@echo off
rem Builds snip.exe (static CRT, no redistributables needed).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE ^
   src\main.cpp src\monitor_select.cpp ^
   /Fo:build\ /Fe:snip.exe /link /SUBSYSTEM:WINDOWS
