@echo off
rem Builds web\sam.wasm: the same C synthesizer, for the browser page.
rem Needs Zig (pip install ziglang).
setlocal
cd /d "%~dp0"
python -m ziglang cc -target wasm32-wasi -mexec-model=reactor -O2 -w -Wl,--strip-all -Isrc -o web\sam.wasm ^
    src\sam.c src\reciter.c src\render.c src\debug.c src\frames.c src\klatt.c src\female.c src\lexicon.c src\rules.c src\glottal.c web\web_api.c || exit /b 1
echo built web\sam.wasm
endlocal
