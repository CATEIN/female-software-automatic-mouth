@echo off
rem Builds the Raspberry Pi Pico configuration for testing on the PC:
rem   sam_fixed.exe  fixed-point Klatt engine (src\klatt_fixed.h)
rem   sam_pico.exe   + streaming output for both engines, as on the Pico
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build\fixed mkdir build\fixed
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DKLATT_FIXED /Fobuild\fixed\ /Fe:sam_fixed.exe src\*.c || exit /b 1
if not exist build\pico mkdir build\pico
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DKLATT_FIXED /DSAM_STREAM /Fobuild\pico\ /Fe:sam_pico.exe src\*.c || exit /b 1
endlocal
