@echo off
REM Run this from a "Developer Command Prompt for VS" so cl.exe and rc.exe are on PATH.
cd /d "%~dp0"
rc /nologo ReadingRuler.rc
cl /nologo /O2 /W3 /DUNICODE /D_UNICODE ReadingRuler.cpp ReadingRuler.res ^
   /link /SUBSYSTEM:WINDOWS /OUT:ReadingRuler.exe user32.lib gdi32.lib shell32.lib ole32.lib
del *.obj *.res 2>nul
echo.
echo Built ReadingRuler.exe
pause
