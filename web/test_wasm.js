// Checks that web/sam.wasm produces the same audio as the native sam.exe,
// and that repeated calls / voice switches in one instance are clean.
//
//   node web/test_wasm.js
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execFileSync } = require("child_process");
require("./sam_loader.js");

const root = path.join(__dirname, "..");
const sam = path.join(root, "sam.exe");
const TEXT = "I am Sam, the software automatic mouth. How are you today?";

function nativeRender(args) {
  const wav = path.join(os.tmpdir(), "sam_native_test.wav");
  execFileSync(sam, [...args, "-wav", wav, ...TEXT.split(" ")]);
  const b = fs.readFileSync(wav);
  const bits = b.readUInt16LE(34);
  const data = b.subarray(44);
  if (bits === 8) return Float32Array.from(data, (x) => (x - 128) / 128);
  const out = new Float32Array(data.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = data.readInt16LE(i * 2) / 32767;
  return out;
}

function compare(name, a, b) {
  const n = Math.min(a.length, b.length);
  let maxDiff = 0, sa = 0, sb = 0, sab = 0;
  for (let i = 0; i < n; i++) {
    maxDiff = Math.max(maxDiff, Math.abs(a[i] - b[i]));
    sa += a[i] * a[i]; sb += b[i] * b[i]; sab += a[i] * b[i];
  }
  const corr = sab / Math.sqrt(sa * sb);
  const ok = a.length === b.length && corr > 0.999;
  console.log(`${ok ? "OK  " : "FAIL"} ${name.padEnd(34)} length ${a.length === b.length ? "same" : a.length + " vs " + b.length}` +
    `, correlation ${corr.toFixed(6)}, max diff ${maxDiff.toFixed(5)}`);
  return ok;
}

(async () => {
  const s = await createSam(fs.readFileSync(path.join(__dirname, "sam.wasm")), (m) => console.log("  [wasm]", m));
  let ok = true;
  console.log(`${s.params.length} parameters exposed`);

  const run = (voice, engine, params = {}) => {
    s.setSam({});
    if (engine === "klatt") {
      s.setVoice(voice);
      for (const [k, v] of Object.entries(params)) s.setParam(k, v);
    }
    return s.speak(TEXT, { engine }).samples;
  };

  ok &= compare("original SAM engine", run(null, "sam"), nativeRender([]));
  ok &= compare("SAM renderer, female data", run(null, "samfemale"), nativeRender(["-voice", "female"]));
  ok &= compare("klatt male", run("male", "klatt"), nativeRender(["-engine", "klatt", "-voice", "male"]));
  ok &= compare("klatt female", run("female", "klatt"), nativeRender(["-engine", "klatt", "-voice", "female"]));
  ok &= compare("female, rd=0.8 f0Scale=2",
    run("female", "klatt", { rd: 0.8, f0Scale: 2 }),
    nativeRender(["-engine", "klatt", "-voice", "female", "-set", "rd=0.8", "-set", "f0Scale=2"]));
  ok &= compare("female, bits=4 hold=2",
    run("female", "klatt", { bits: 4, hold: 2 }),
    nativeRender(["-engine", "klatt", "-voice", "female", "-bits", "4", "-hold", "2"]));

  // same instance, repeated and interleaved calls must not leak state
  const first = run("female", "klatt");
  run("male", "klatt", { breath: 1.5, flutter: 80 });
  run(null, "sam");
  run(null, "samfemale");
  ok &= compare("original SAM after the female one", run(null, "sam"), nativeRender([]));
  ok &= compare("female again after other voices", run("female", "klatt"), first);

  const long = s.speak("This is a much longer piece of text that goes well past the two hundred and fifty " +
    "character limit of the original program, so the loader has to split it into several pieces. " +
    "Each piece is rendered in turn and the audio is joined together. Does it work? It should.", { engine: "klatt" });
  console.log(`${long.samples.length > 22050 * 10 ? "OK  " : "FAIL"} long text: ${(long.samples.length / 22050).toFixed(1)} s`);
  console.log("phonemes:", s.speak("Hello there.", {}).phonemes);
  console.log(ok ? "\nall checks passed" : "\nSOME CHECKS FAILED");
  process.exit(ok ? 0 : 1);
})();
