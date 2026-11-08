@echo off
setlocal
cd /d "%~dp0"
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "PATH=%MSYS2_ROOT%\ucrt64\bin;%PATH%"
if not exist "build\rpg_3d.exe" (
    call build.cmd
    if errorlevel 1 exit /b 1
)
"build\rpg_3d.exe"
