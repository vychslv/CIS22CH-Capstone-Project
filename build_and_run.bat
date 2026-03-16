@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
set "PROJECT_ROOT=%~dp0"
if "!PROJECT_ROOT:~-1!"=="\" set "PROJECT_ROOT=!PROJECT_ROOT:~0,-1!"
set "VCPKG=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake"
set "VSDEV="
set "USED_SUBST="
set "DRIVE_LETTER="

:: If path has non-ASCII (e.g. Cyrillic), use a virtual drive so the compiler sees an ASCII path
echo !PROJECT_ROOT!| findstr /R "^[A-Za-z0-9\\:_\.\-]*$" >nul
if errorlevel 1 (
    set "USED_SUBST=1"
    for %%D in (Z Y X W V) do (
        if "!DRIVE_LETTER!"=="" (
            subst %%D: "!PROJECT_ROOT!" 2>nul
            if exist "%%D:\" (
                set "DRIVE_LETTER=%%D"
                set "PROJECT_DIR=%%D:\"
            )
        )
    )
    if "!DRIVE_LETTER!"=="" (
        echo Could not create virtual drive for project path. Try moving the folder to C:\CapstoneProject
        pause
        exit /b 1
    )
    echo Using drive !DRIVE_LETTER!: for build ^(project stays in place^).
) else (
    set "PROJECT_DIR=!PROJECT_ROOT!\"
)
set "BUILD_DIR=%PROJECT_DIR%build"

:: Try VS 2022 Community
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEV=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEV if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
    set "VSDEV=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEV if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
)

if "%VSDEV%"=="" (
    echo Visual Studio 2022 with C++ not found. Install "Desktop development with C++" then try again.
    pause
    exit /b 1
)

cd /d "%PROJECT_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo Stopping any running server so the build can overwrite the .exe...
taskkill /F /IM air_travel_app.exe 2>nul
timeout /t 1 /nobreak >nul
echo Configuring and building...
call "%VSDEV%" -arch=amd64
if errorlevel 1 (
    echo VsDevCmd failed.
    if "!USED_SUBST!"=="1" subst !DRIVE_LETTER!: /D
    pause
    exit /b 1
)
cmake .. -DCMAKE_TOOLCHAIN_FILE="%VCPKG%"
if errorlevel 1 (
    echo CMake configure failed. Check vcpkg path: %VCPKG%
    if "!USED_SUBST!"=="1" subst !DRIVE_LETTER!: /D
    pause
    exit /b 1
)
cmake --build . --config Release
if errorlevel 1 (
    echo Build failed.
    if "!USED_SUBST!"=="1" subst !DRIVE_LETTER!: /D
    pause
    exit /b 1
)

echo.
echo Build OK. Starting server on http://localhost:8080
echo Open that address in your browser. Close this window to stop the server.
echo.
cd /d "%PROJECT_DIR%"
if exist "%BUILD_DIR%\Release\air_travel_app.exe" (
    "%BUILD_DIR%\Release\air_travel_app.exe"
) else if exist "%BUILD_DIR%\air_travel_app.exe" (
    "%BUILD_DIR%\air_travel_app.exe"
) else (
    echo Executable not found.
    if "!USED_SUBST!"=="1" subst !DRIVE_LETTER!: /D
    pause
    exit /b 1
)
if "!USED_SUBST!"=="1" subst !DRIVE_LETTER!: /D
exit /b 0
