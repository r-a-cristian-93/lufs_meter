@echo off

echo =========================
echo Building SDL LUFS app (MSYS2)
echo =========================

REM IMPORTANT: adjust path if needed
set MINGW=C:\msys64\mingw64

SET PATH=%MINGW%\bin;%PATH%

g++ main.cpp -o app.exe ^
    -IC:\msys64\mingw64\include ^
    -LC:\msys64\mingw64\lib ^
    -lmingw32 ^
    -lSDL2main ^
    -lSDL2 ^
    -lsndfile ^
    -lebur128


if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b
)

echo.
echo Running app...
start "" app.exe audio.wav
