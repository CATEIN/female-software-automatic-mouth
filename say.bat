@echo off
rem Speak with the female voice and play the result.
rem   say Hello there, how are you?
rem   say Hello there -range 1.2 -speed 60
rem   say -voice male Hello there
rem Writes out\say.wav each time (overwritten).
if "%~1"=="" (
    echo usage: say [options] text...
    "%~dp0sam.exe"
    exit /b 1
)
"%~dp0sam.exe" -engine klatt -voice female -wav "%~dp0out\say.wav" %*
if errorlevel 1 exit /b 1
start "" "%~dp0out\say.wav"
