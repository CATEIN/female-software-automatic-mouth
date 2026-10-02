"""Builds the pronunciation data for engine A (src/lexdata.h) from CMUdict.

    python tools/gen_lexicon.py [--leaf N]      (minimum words per tree leaf, default 12)

1. Letters are aligned one-to-one with phones by Viterbi EM: each letter
   becomes no phone, one phone, or one of a few phone pairs (x -> K S,
   u -> Y UW, ...), as in Black, Lenzo & Pagel, "Issues in building general
   letter to sound rules" (1998).
2. One decision tree per letter predicts its phone (vowels with stress) from
   the letters around it (3 on each side), like Flite's letter-to-sound CART.
3. Every word the trees get wrong is stored in an exceptions lexicon, so
   dictionary words come out exactly as in CMUdict and unknown words still get
   a reasonable guess.

Output: src/lexdata.h with the trees and the front-coded exception list, and
a size/accuracy report. CMUdict: data/cmudict.dict (BSD-style licence, see
data/cmudict.LICENSE).
"""
import math, os, re, sys, time
from collections import Counter, defaultdict
import numpy as np
from sklearn.tree import DecisionTreeClassifier

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DICT = os.path.join(ROOT, "data", "cmudict.dict")
OUT = os.path.join(ROOT, "src", "lexdata.h")

PHONES = ("AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW "
          "B CH D DH F G HH JH K L M N NG P R S SH T TH V W Y Z ZH").split()
VOWELS = set(PHONES[:15])
PIDX = {p: i + 1 for i, p in enumerate(PHONES)}          # 1..39, 0 = none
PAIRS = {("K", "S"), ("G", "Z"), ("Y", "UW"), ("Y", "UH"), ("Y", "AH"), ("W", "AH"), ("Y", "ER"),
         ("K", "SH"), ("G", "ZH"), ("AH", "L"), ("AH", "M"), ("AH", "N"), ("W", "AA"), ("Y", "AA"),
         ("IH", "Z"), ("AH", "Z"), ("AH", "S")}
LETTERS = "abcdefghijklmnopqrstuvwxyz'"
WINDOW = 3


def load():
    words = {}
    for line in open(DICT, encoding="utf-8"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        w, *ph = line.split()
        if "(" in w or not re.fullmatch(r"[a-z']+", w) or len(w) > 24:
            continue
        words[w] = ph
    return words


def base(p):
    return p.rstrip("012")


def align_all(words, iters=4):
    """Viterbi EM alignment. Returns {word: [output per letter]} where an
    output is a tuple of 0, 1 or 2 phones (with stress)."""
    # initial costs from co-occurrence
    co = defaultdict(Counter)
    for w, ph in words.items():
        bs = set(base(p) for p in ph)
        for l in set(w):
            for b in bs:
                co[l][b] += 1
    prob = {}
    for l, c in co.items():
        tot = sum(c.values())
        prob[l] = {(b,): v / tot for b, v in c.items()}
        prob[l][()] = 0.2
    aligned = {}
    for it in range(iters):
        counts = defaultdict(Counter)
        aligned = {}
        for w, ph in words.items():
            a = viterbi(w, ph, prob)
            if a is None:
                continue
            aligned[w] = a
            for l, out in zip(w, a):
                counts[l][tuple(base(p) for p in out)] += 1
        prob = {}
        for l, c in counts.items():
            tot = sum(c.values())
            prob[l] = {k: (v + 0.1) / tot for k, v in c.items()}
        print(f"  alignment pass {it + 1}: {len(aligned)}/{len(words)} words aligned", flush=True)
    return aligned


def viterbi(w, ph, prob):
    n, m = len(w), len(ph)
    if m > 2 * n:
        return None
    INF = 1e18
    cost = [[INF] * (m + 1) for _ in range(n + 1)]
    back = [[None] * (m + 1) for _ in range(n + 1)]
    cost[0][0] = 0
    bp = [base(p) for p in ph]
    for i in range(n):
        pl = prob.get(w[i], {})
        for j in range(m + 1):
            c0 = cost[i][j]
            if c0 >= INF:
                continue
            # letter -> nothing
            p = pl.get((), 1e-4)
            c = c0 - math.log(p)
            if c < cost[i + 1][j]:
                cost[i + 1][j], back[i + 1][j] = c, (j, 0)
            if j < m:
                p = pl.get((bp[j],), 1e-6)
                c = c0 - math.log(p)
                if c < cost[i + 1][j + 1]:
                    cost[i + 1][j + 1], back[i + 1][j + 1] = c, (j, 1)
            if j + 1 < m and (bp[j], bp[j + 1]) in PAIRS:
                p = pl.get((bp[j], bp[j + 1]), 1e-5)
                c = c0 - math.log(p)
                if c < cost[i + 1][j + 2]:
                    cost[i + 1][j + 2], back[i + 1][j + 2] = c, (j, 2)
    if cost[n][m] >= INF:
        return None
    out, j = [], m
    for i in range(n, 0, -1):
        pj, k = back[i][j]
        out.append(tuple(ph[pj:pj + k]))
        j = pj
    return out[::-1]


def features(w, i):
    pad = "#" * WINDOW + w + "#" * WINDOW
    return [pad[i + k] for k in range(2 * WINDOW + 1)]   # letter i is at index WINDOW


SYMS = "#" + LETTERS


def encode(ctx):
    v = np.zeros(len(ctx) * len(SYMS), dtype=np.uint8)
    for k, ch in enumerate(ctx):
        v[k * len(SYMS) + SYMS.index(ch)] = 1
    return v


def main():
    leaf = int(sys.argv[sys.argv.index("--leaf") + 1]) if "--leaf" in sys.argv else 12
    t0 = time.time()
    words = load()
    print(f"{len(words)} words from CMUdict")
    aligned = align_all(words)
    # training data per letter
    data = defaultdict(lambda: ([], []))
    classes = {}
    for w, a in aligned.items():
        for i, out in enumerate(a):
            key = " ".join(out)
            cls = classes.setdefault(key, len(classes))
            X, y = data[w[i]]
            X.append(encode(features(w, i)))
            y.append(cls)
    names = [None] * len(classes)
    for k, v in classes.items():
        names[v] = k
    print(f"{len(names)} output classes; training trees (min_samples_leaf={leaf})", flush=True)
    trees = {}
    for l in LETTERS:
        if l not in data:
            continue
        X, y = data[l]
        t = DecisionTreeClassifier(min_samples_leaf=leaf, random_state=0)
        t.fit(np.array(X), np.array(y))
        trees[l] = t

    # predict every word; wrong ones become exceptions
    def predict(w):
        out = []
        for i, l in enumerate(w):
            t = trees.get(l)
            if t is None:
                continue
            cls = t.classes_[int(np.argmax(t.predict_proba(encode(features(w, i))[None])[0]))]
            if names[cls]:
                out += names[cls].split()
        return out

    # batch prediction for speed
    exceptions = {}
    by_letter = defaultdict(list)
    for w in words:
        for i, l in enumerate(w):
            by_letter[l].append((w, i))
    pred = {}
    for l, items in by_letter.items():
        t = trees[l]
        X = np.array([encode(features(w, i)) for w, i in items])
        cls = t.classes_[t.predict(X).astype(int)] if False else t.predict(X)
        for (w, i), c in zip(items, cls):
            pred[(w, i)] = names[int(c)]
    for w, ph in words.items():
        out = []
        for i in range(len(w)):
            if pred[(w, i)]:
                out += pred[(w, i)].split()
        if out != ph:
            exceptions[w] = ph
    nodes = sum(t.tree_.node_count for t in trees.values())
    correct = 1 - len(exceptions) / len(words)
    print(f"trees: {nodes} nodes; words right from rules alone: {correct * 100:.1f}%; exceptions: {len(exceptions)}")
    write_header(trees, names, exceptions, len(words))
    print(f"done in {time.time() - t0:.0f} s")


def phone_byte(p):
    b = base(p)
    stress = int(p[-1]) + 1 if p[-1].isdigit() else 0          # 0 none, 1..3 = stress 0..2
    return PIDX[b] | (stress << 6)


SMALL_TOP = 40000     # Pico: keep exceptions for the 40,000 most frequent English words


def pack_exceptions(exceptions, words):
    """Sorted, front-coded in blocks of 32. Entry: (shared << 4 | suffix length,
    15 = length in the next byte), suffix letters, phone count, phones."""
    blob, index, prev = bytearray(), [], ""
    for k, w in enumerate(sorted(words)):
        if k % 32 == 0:
            index.append(len(blob))
            prev = ""
        shared = 0
        while shared < min(len(prev), len(w), 15) and prev[shared] == w[shared]:
            shared += 1
        suffix = w[shared:]
        if len(suffix) >= 15:
            blob += bytes([shared << 4 | 15, len(suffix)])
        else:
            blob += bytes([shared << 4 | len(suffix)])
        blob += suffix.encode() + bytes([len(exceptions[w])]) + bytes(phone_byte(p) for p in exceptions[w])
        prev = w
    return blob, index


def write_header(trees, names, exceptions, nwords):
    from wordfreq import top_n_list
    # classes: up to 2 phone bytes each
    cls_bytes = []
    for n in names:
        ps = n.split() if n else []
        b = [phone_byte(p) for p in ps] + [0, 0]
        cls_bytes.append(b[:2])
    # trees: one per letter, indices relative to its first node.
    # test byte = pos * 28 + symbol ("is the letter at pos this symbol?"), 255 = leaf
    tests, no, yes, roots = [], [], [], {}
    for l in LETTERS:
        if l not in trees:
            roots[l] = 0xFFFFFFFF
            continue
        t = trees[l].tree_
        assert t.node_count < 65536
        roots[l] = len(tests)
        for k in range(t.node_count):
            if t.children_left[k] == -1:
                tests.append(255)
                no.append(int(trees[l].classes_[int(np.argmax(t.value[k][0]))]))
                yes.append(0)
            else:
                pos, sym = divmod(int(t.feature[k]), len(SYMS))
                tests.append(pos * len(SYMS) + sym)
                # sklearn: left = feature <= 0.5 (absent), right = present
                no.append(int(t.children_left[k]))
                yes.append(int(t.children_right[k]))
    freq = set(top_n_list("en", SMALL_TOP))
    full_blob, full_index = pack_exceptions(exceptions, exceptions)
    small_words = [w for w in exceptions if w in freq]
    small_blob, small_index = pack_exceptions(exceptions, small_words)
    size_tree = len(tests) * 5 + len(cls_bytes) * 2
    print(f"sizes: trees {size_tree / 1024:.0f} KB ({len(tests)} nodes); "
          f"exceptions full {len(full_blob) / 1024:.0f} KB ({len(exceptions)} words), "
          f"small {len(small_blob) / 1024:.0f} KB ({len(small_words)} words of the top {SMALL_TOP})")

    def arr(vals, per=24):
        out = ""
        for i in range(0, len(vals), per):
            out += "    " + ",".join(str(v) for v in vals[i:i + per]) + ",\n"
        return out

    with open(OUT, "w", newline="\n") as f:
        f.write("// Generated by tools/gen_lexicon.py from CMUdict (data/cmudict.dict,\n"
                "// Copyright (C) 1993-2015 Carnegie Mellon University, BSD-style licence:\n"
                "// see data/cmudict.LICENSE). Do not edit.\n")
        f.write(f"// {nwords} words: letter-to-sound trees, plus the words they get wrong:\n"
                f"// all {len(exceptions)} of them, or with LEX_SMALL (microcontrollers) only the\n"
                f"// {len(small_words)} among the {SMALL_TOP} most frequent English words (wordfreq).\n\n")
        f.write("#define LEX_SYMS \"" + SYMS + "\"\n")
        f.write(f"#define LEX_WINDOW {WINDOW}\n#define LEX_NSYMS {len(SYMS)}\n\n")
        f.write("// per letter (a-z, '): first node of its tree, 0xFFFFFFFF = none\n")
        f.write("static const unsigned int lexRoot[27] = {\n" + arr([roots[l] for l in LETTERS], 9) + "};\n\n")
        f.write("// nodes: test (pos * LEX_NSYMS + symbol, 255 = leaf), then the next node\n"
                "// (relative to the tree) when the letter at pos is not / is that symbol;\n"
                "// a leaf keeps its output class in lexNo\n")
        f.write(f"static const unsigned char lexTest[{len(tests)}] = {{\n" + arr(tests, 40) + "};\n")
        f.write(f"static const unsigned short lexNo[{len(no)}] = {{\n" + arr(no, 24) + "};\n")
        f.write(f"static const unsigned short lexYes[{len(yes)}] = {{\n" + arr(yes, 24) + "};\n\n")
        f.write("// output classes: up to two phones (index | stress code << 6)\n")
        f.write(f"static const unsigned char lexClass[{len(cls_bytes)}][2] = {{\n" +
                arr([f"{{{a},{b}}}" for a, b in cls_bytes], 16) + "};\n\n")
        for tag, blob, index in (("#ifndef LEX_SMALL", full_blob, full_index), ("#else", small_blob, small_index)):
            f.write(tag + "\n")
            f.write(f"#define LEX_BLOCKS {len(index)}\n")
            f.write(f"static const unsigned int lexIndex[{len(index)}] = {{\n" + arr(index, 12) + "};\n")
            f.write(f"static const unsigned char lexData[{len(blob)}] = {{\n" + arr(list(blob), 40) + "};\n")
        f.write("#endif\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
