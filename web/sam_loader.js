// Loads web/sam.wasm (the C synthesizer built by build_web.bat) and wraps it
// in a small API. Used by the web page and by web/test_wasm.js (Node).
//
//   const sam = await createSam(wasmBytes);
//   sam.setVoice("female");
//   sam.setParam("rd", 1.4);
//   const { samples, phonemes } = sam.speak("Hello there.");
(function (root) {
  "use strict";

  async function createSam(wasmBytes, log) {
    log = log || function () {};
    let memory = null;
    const decoder = new TextDecoder();
    const encoder = new TextEncoder();

    // The C code only uses stdio for diagnostics, so four WASI calls suffice.
    const wasi = {
      fd_write(fd, iovs, iovsLen, nwrittenPtr) {
        const view = new DataView(memory.buffer);
        let written = 0, text = "";
        for (let i = 0; i < iovsLen; i++) {
          const ptr = view.getUint32(iovs + i * 8, true);
          const len = view.getUint32(iovs + i * 8 + 4, true);
          text += decoder.decode(new Uint8Array(memory.buffer, ptr, len));
          written += len;
        }
        view.setUint32(nwrittenPtr, written, true);
        if (text.trim()) log(text.trim());
        return 0;
      },
      fd_close() { return 0; },
      fd_seek() { return 70; }, // ESPIPE
      fd_fdstat_get(fd, statPtr) {
        new Uint8Array(memory.buffer, statPtr, 24).fill(0);
        new DataView(memory.buffer).setUint8(statPtr, 2); // character device
        return 0;
      },
    };

    const { instance } = await WebAssembly.instantiate(wasmBytes, { wasi_snapshot_preview1: wasi });
    const e = instance.exports;
    memory = e.memory;
    e._initialize();

    const sampleRate = e.web_sample_rate();

    function readString(ptr) {
      const bytes = new Uint8Array(memory.buffer, ptr);
      let end = 0;
      while (bytes[end] !== 0) end++;
      return decoder.decode(bytes.subarray(0, end));
    }

    function writeString(ptr, capacity, s) {
      const bytes = encoder.encode(s).subarray(0, capacity - 1);
      const dst = new Uint8Array(memory.buffer, ptr, capacity);
      dst.fill(0);
      dst.set(bytes);
      return ptr;
    }

    const scratch = (s) => writeString(e.web_scratch_buffer(), 128, s);

    const params = [];
    for (let i = 0; i < e.web_param_count(); i++) {
      params.push({
        name: readString(e.web_param_name(i)),
        group: readString(e.web_param_group(i)),
        help: readString(e.web_param_help(i)),
        min: e.web_param_min(i),
        max: e.web_param_max(i),
      });
    }

    function setVoice(name) {
      if (!e.web_set_voice(scratch(name))) throw new Error("unknown voice " + name);
    }
    function setParam(name, value) {
      if (!e.web_set_param(scratch(name), value)) throw new Error("unknown parameter " + name);
    }
    function getParam(name) { return e.web_get_param(scratch(name)); }
    function getParams() {
      const out = {};
      for (const p of params) out[p.name] = getParam(p.name);
      return out;
    }
    function setSam(o) {
      e.web_set_sam(o.speed ?? 72, o.pitch ?? 64, o.mouth ?? 128, o.throat ?? 128, o.sing ? 1 : 0);
    }

    // SAM reads at most ~250 characters per call: split longer text at
    // sentence ends, then commas/spaces, and render the pieces in turn.
    function splitText(text, max) {
      const pieces = [];
      let rest = text.trim();
      while (rest.length > max) {
        let cut = -1;
        for (const re of [/[.!?]\s/g, /[,;:]\s/g, /\s/g]) {
          let m;
          while ((m = re.exec(rest)) && m.index < max) cut = m.index + 1;
          if (cut > 0) break;
        }
        if (cut <= 0) cut = max;
        pieces.push(rest.slice(0, cut).trim());
        rest = rest.slice(cut).trim();
      }
      if (rest) pieces.push(rest);
      return pieces;
    }

    // engine: "klatt" (male/female voices), "sam" (original 1982 renderer) or
    // "samfemale" (1982 renderer, female data)
    function speak(text, opts) {
      opts = opts || {};
      // "sam": original renderer, "samfemale": the same renderer with female
      // data (the C64 voice), anything else: the Klatt voices
      const engine = opts.engine === "sam" ? 0 : opts.engine === "samfemale" ? 2 : 1;
      const phonetic = !!opts.phonetic;
      // SAM understands plain ASCII only
      const clean = text.replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
        .replace(/[–—]/g, ", ").replace(/[^\x20-\x7e]/g, " ");
      const pieces = phonetic ? [clean.slice(0, 240)] : splitText(clean, 200);
      const chunks = [], phonemes = [];
      let total = 0;
      for (const piece of pieces) {
        writeString(e.web_text_buffer(), e.web_text_capacity(), piece);
        const n = e.web_speak(phonetic ? 1 : 0, engine);
        if (n < 0) throw new Error("SAM could not read: " + piece);
        chunks.push(new Float32Array(memory.buffer, e.web_samples(), n).slice());
        phonemes.push(readString(e.web_phonemes()));
        total += n;
      }
      const samples = new Float32Array(total);
      let pos = 0;
      for (const c of chunks) { samples.set(c, pos); pos += c.length; }
      return { samples, sampleRate, phonemes: phonemes.join(" ") };
    }

    return { params, setVoice, setParam, getParam, getParams, setSam, speak, sampleRate };
  }

  // 16-bit mono WAV from float samples
  function encodeWav(samples, sampleRate) {
    const buf = new ArrayBuffer(44 + samples.length * 2);
    const v = new DataView(buf);
    const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
    str(0, "RIFF"); v.setUint32(4, 36 + samples.length * 2, true); str(8, "WAVEfmt ");
    v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sampleRate, true); v.setUint32(28, sampleRate * 2, true);
    v.setUint16(32, 2, true); v.setUint16(34, 16, true); str(36, "data");
    v.setUint32(40, samples.length * 2, true);
    for (let i = 0; i < samples.length; i++) {
      const x = Math.max(-1, Math.min(1, samples[i]));
      v.setInt16(44 + i * 2, Math.round(x * 32767), true);
    }
    return buf;
  }

  root.createSam = createSam;
  root.encodeWav = encodeWav;
})(typeof window !== "undefined" ? window : globalThis);
