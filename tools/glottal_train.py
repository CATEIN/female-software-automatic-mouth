"""Trains the neural voice source: a tiny network that predicts the spectrum
of each glottal pulse from what the synthesizer knows when a period starts.

    python tools/glottal_train.py [--k 8] [--hidden 16] [--harmonics 24] [--epochs 40]

Input: out/glottal/pulses.npz from tools/glottal_extract.py (CMU ARCTIC slt).

Why a spectrum: predicting the pulse waveform gives the average waveform of
each context, and averaging periods whose opening peak varies in timing
cancels the upper harmonics (the average pulse's H2 is ~25 dB weaker than in
real pulses: an over-smooth, sine-like source). Log harmonic magnitudes
average without cancelling, so the network predicts those, and the pulse is
rebuilt with the data's circular-mean phase per harmonic:
  1. per period, the magnitudes of harmonics 1..M (log2) -> PCA, K components
  2. features (all known to klatt.c at the period start): log2(F0 / 190 Hz),
     level re. loud vowels (dB / 20), nasal, voiced fricative, periods since
     voicing began (/ 10), level trend (dB / 10)
  3. network 6 -> H (tanh) -> K, Adam, 90% of the utterances; 10% held out
Output: src/glottal_model.h and a report of the harmonic levels of real
pulses, of the network's pulses and of the old waveform average.
"""
import os, sys
import numpy as np
import torch

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DATA = os.path.join(ROOT, "out", "glottal", "pulses.npz")
OUT = os.path.join(ROOT, "src", "glottal_model.h")


def arg(name, default):
    return type(default)(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def features(F):
    f0, lev, nasal, vfric, run, trend = F.T
    return np.stack([np.log2(f0 / 190.0), np.clip(lev, -40, 3) / 20, nasal, vfric,
                     np.minimum(run, 10) / 10, np.clip(trend, -20, 20) / 10], 1).astype(np.float32)


def main():
    K, H, M, epochs = arg("--k", 8), arg("--hidden", 16), arg("--harmonics", 24), arg("--epochs", 40)
    d = np.load(DATA)
    P, F, U = d["pulses"], d["features"], d["utt"]
    # only harmonics inside the recordings' band (8 kHz) at every period's F0
    ok = F[:, 0] * M < 7900
    P, F, U = P[ok], F[ok], U[ok]
    X = features(F)
    S = np.fft.rfft(P, axis=1)[:, 1:M + 1]
    L = np.log2(np.abs(S) + 1e-6)                       # log2 magnitudes
    phase = np.angle(np.sum(S / (np.abs(S) + 1e-9), axis=0))   # circular mean phase
    coherence = np.abs(np.mean(S / (np.abs(S) + 1e-9), axis=0))
    test = (U % 10) == 9
    mean = L[~test].mean(0)
    _, sv, Vt = np.linalg.svd(L[~test] - mean, full_matrices=False)
    var = sv ** 2 / np.sum(sv ** 2)
    print(f"{len(P)} periods; harmonic phase coherence 1-4: {np.round(coherence[:4], 2)}")
    for k in (2, 4, 8, 12):
        print(f"PCA {k:2d} components: {np.sum(var[:k]) * 100:5.1f}% of the log-spectrum variance")
    B = Vt[:K]
    W = (L - mean) @ B.T
    wstd = W[~test].std(0)
    torch.manual_seed(0)
    net = torch.nn.Sequential(torch.nn.Linear(X.shape[1], H), torch.nn.Tanh(), torch.nn.Linear(H, K))
    opt = torch.optim.Adam(net.parameters(), lr=3e-3)
    Xt, Wt = torch.tensor(X[~test]), torch.tensor(W[~test] / wstd, dtype=torch.float32)
    Xv, Wv = torch.tensor(X[test]), torch.tensor(W[test] / wstd, dtype=torch.float32)
    wvar = torch.tensor(wstd ** 2 / np.sum(wstd ** 2), dtype=torch.float32)
    for ep in range(epochs):
        perm = torch.randperm(len(Xt))
        for i in range(0, len(Xt), 1024):
            idx = perm[i:i + 1024]
            loss = (((net(Xt[idx]) - Wt[idx]) ** 2) * wvar).sum(1).mean()
            opt.zero_grad(); loss.backward(); opt.step()
    with torch.no_grad():
        Wp = net(torch.tensor(X)).numpy() * wstd
        lv = (((net(Xv) - Wv) ** 2) * wvar).sum(1).mean().item()
    Lp = mean + Wp @ B
    base = np.mean((L[test] - mean) ** 2)
    print(f"held-out log-spectrum error: mean spectrum alone {base:.3f}, network {np.mean((L[test] - Lp[test]) ** 2):.3f} "
          f"(log2 units^2); validation loss {lv:.3f}")
    # harmonic levels: real pulses vs rebuilt from the network vs the waveform average
    real = 10 * np.log10(np.mean(np.abs(S[test]) ** 2, axis=0))
    rebuilt = 10 * np.log10(np.mean((2.0 ** Lp[test]) ** 2, axis=0))
    avgwave = 10 * np.log10(np.abs(np.fft.rfft(P[test].mean(0))[1:M + 1]) ** 2)
    print("harmonic   real   network   (old) waveform average   [dB re H1]")
    for k in (1, 2, 3, 5, 10, 20):
        if k <= M:
            print(f"  {k:3d}   {real[k - 1] - real[0]:6.1f}   {rebuilt[k - 1] - rebuilt[0]:6.1f}   {avgwave[k - 1] - avgwave[0]:6.1f}")
    nparams = sum(p.numel() for p in net.parameters())
    print(f"network: {X.shape[1]} -> {H} -> {K}, {nparams} weights; tables: {M} mean log magnitudes, "
          f"{K}x{M} components, {M} phases")
    export(net, mean, B, phase, K, H, M, nparams, X.shape[1])


def export(net, mean, B, phase, K, H, M, nparams, nin):
    W1 = net[0].weight.detach().numpy(); b1 = net[0].bias.detach().numpy()
    W2 = net[2].weight.detach().numpy(); b2 = net[2].bias.detach().numpy()

    def arr(v, fmt="%.7ef"):
        return ",".join(fmt % x for x in np.asarray(v).ravel())

    with open(OUT, "w", newline="\n") as f:
        f.write("// Generated by tools/glottal_train.py from CMU ARCTIC slt glottal pulses\n"
                "// (tools/glottal_extract.py). Do not edit.\n"
                f"// Harmonic k of a period (k = 1..GM_M): magnitude 2^(gmMean[k] + sum of\n"
                f"// components x weights), phase gmPhase[k]; weights = network({nin} features).\n"
                f"// Network {nin} -> {H} (tanh) -> {K}: {nparams} weights.\n\n")
        f.write(f"#define GM_M {M}\n#define GM_K {K}\n#define GM_IN {nin}\n#define GM_H {H}\n\n")
        f.write(f"static const float gmMean[GM_M] = {{{arr(mean)}}};\n")
        f.write(f"static const float gmBasis[GM_K][GM_M] = {{{arr(B)}}};\n")
        f.write(f"static const float gmPhase[GM_M] = {{{arr(phase)}}};\n")
        f.write(f"static const float gmW1[GM_H][GM_IN] = {{{arr(W1)}}};\n")
        f.write(f"static const float gmB1[GM_H] = {{{arr(b1)}}};\n")
        f.write(f"static const float gmW2[GM_K][GM_H] = {{{arr(W2)}}};\n")
        f.write(f"static const float gmB2[GM_K] = {{{arr(b2)}}};\n")
    print("wrote", OUT)


main()
