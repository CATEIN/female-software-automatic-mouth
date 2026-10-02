@echo off
rem Starts female_sam.prg in VICE with a 6581 SID (needed for audible
rem volume-register samples). Set VICE_HOME if VICE is installed elsewhere.
if "%VICE_HOME%"=="" set VICE_HOME=%USERPROFILE%\GTK3VICE-3.9-win64
start "" "%VICE_HOME%\bin\x64sc.exe" -sidmodel 0 -autostartprgmode 1 -autostart "%~dp0female_sam.prg"
