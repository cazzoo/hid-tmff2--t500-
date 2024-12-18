@echo off
setlocal enabledelayedexpansion

echo Checking for WSL...
wsl --version >nul 2>&1
if errorlevel 1 (
    echo Error: WSL is not installed
    echo Please install WSL using: wsl --install
    exit /b 1
)

echo Checking if we're in the correct directory...
if not exist meson.build (
    echo Error: meson.build not found
    echo Please run this script from the t500rs-test directory
    exit /b 1
)

echo Building application using WSL...
wsl ./build.sh
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo Build completed successfully!
echo You can now run the application using: run.bat
