@echo off
rem Builds dist\freesnip.exe (wxWidgets, static CRT, no redistributables needed).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-windows.ps1" %*
