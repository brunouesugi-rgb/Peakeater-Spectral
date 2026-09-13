@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-peakeater-spectral.ps1" %*
if errorlevel 1 pause
