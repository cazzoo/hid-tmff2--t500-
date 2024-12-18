@echo off
setlocal enabledelayedexpansion

echo Checking for WSL...
wsl --version >nul 2>&1
if errorlevel 1 (
    echo Error: WSL is not installed
    echo Please install WSL using: wsl --install
    exit /b 1
)

echo Checking if application is built...
if not exist builddir\t500rs-test (
    echo Error: Application not built
    echo Please run build.bat first
    exit /b 1
)

echo Running application using WSL...
wsl ./run.sh
