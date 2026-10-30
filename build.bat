@echo off
rem Builds dist\snip.exe (wxWidgets, static CRT, no redistributables needed).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-windows.ps1" %*
