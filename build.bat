@echo off

echo =========================
echo Building SDL LUFS app (MSYS2)
echo =========================

REM Set MinGW path
set PATH=C:\msys64\mingw64\bin;%PATH%

REM Get flags dynamically
FOR /F "tokens=*" %%i IN ('pkg-config --cflags sdl2 SDL2_ttf sndfile libebur128') DO SET CFLAGS=%%i
FOR /F "tokens=*" %%i IN ('pkg-config --libs sdl2 SDL2_ttf sndfile libebur128') DO SET LIBS=%%i

echo.
echo Compiling...
g++ main.cpp -o ./app/app.exe %CFLAGS% %LIBS% -std=c++17 -O2

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b
)

echo.
echo Copying runtime DLLs...
xcopy C:\msys64\mingw64\bin\*.dll .\app\dll /Y >nul
