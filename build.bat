@echo off
rem Builds sam.exe with the MSVC Build Tools (no SDL; use -wav for output).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:sam.exe src\*.c || exit /b 1
endlocal
