LUFS SDL Analyzer

Setup Instructions

Requirements:
- MSYS2 (https://www.msys2.org/)
- C++17 compiler (MinGW via MSYS2)

1. Install MSYS2
Install and update:
```
pacman -Syu
```
(restart terminal, then run again)
```
pacman -Syu
```

2. Open correct terminal:
Use: MinGW64 (mingw64.exe)

3. Install dependencies:
```
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-libsndfile mingw-w64-x86_64-libebur128 mingw-w64-x86_64-pkg-config mingw-w64-x86_64-toolchain
```

Build (simple):
```
run  build.bat
```

Run:
```
./app.exe yourfile.wav
```

Controls:
Space = Play/Pause
Mouse click = Seek

