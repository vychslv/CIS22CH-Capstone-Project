@echo off
chcp 65001 >nul
set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"

cd /d "%PROJECT_DIR%"
if exist "%BUILD_DIR%\Release\air_travel_app.exe" (
    echo Starting server at http://localhost:8080 - open in browser. Close this window to stop.
    "%BUILD_DIR%\Release\air_travel_app.exe"
) else if exist "%BUILD_DIR%\air_travel_app.exe" (
    echo Starting server at http://localhost:8080 - open in browser. Close this window to stop.
    "%BUILD_DIR%\air_travel_app.exe"
) else (
    echo Run build_and_run.bat first to build the project.
    pause
)
