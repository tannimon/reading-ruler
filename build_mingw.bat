@echo off
REM For MinGW-w64 / MSYS2. Needs g++ and windres on PATH.
cd /d "%~dp0"
windres ReadingRuler.rc -O coff -o resource.o
g++ -O2 -Wall -municode -mwindows -static -s -o ReadingRuler.exe ReadingRuler.cpp resource.o ^
    -luser32 -lgdi32 -lshell32 -lole32
del resource.o 2>nul
echo.
echo Built ReadingRuler.exe
pause
