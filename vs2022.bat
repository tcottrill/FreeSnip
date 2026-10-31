@echo off
rem Generates build\vs2022\FreeSnip.sln for Visual Studio 2022 and opens it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\make-vs2022.ps1" -Open %*
