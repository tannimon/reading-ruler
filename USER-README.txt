READING RULER - native Windows build
====================================

ReadingRuler.exe is the whole program. 81 KB, no installer, no runtime,
no dependencies. Put it anywhere and double-click it.

Windows may show a blue "Windows protected your PC" box, because the file
is unsigned. Click More info, then Run anyway.


CONTROLS
--------
Ctrl + Alt + R      ruler on / off
Ctrl + Alt + L      lock the band where it is
Ctrl + Alt + Up     taller band
Ctrl + Alt + Down   shorter band
Ctrl + Alt + Left   narrower band
Ctrl + Alt + Right  wider band
Ctrl + Alt + F      back to full screen width
Ctrl + Alt + ]      more dimming
Ctrl + Alt + [      less dimming
Ctrl + Alt + Q      quit

There is also a tray icon by the clock. Left-click toggles the ruler,
right-click gives you lock and quit. If another program has already taken
one of the shortcuts, a message on startup names it.

Narrowing the band turns it into a reading window that tracks your mouse
sideways as well as vertically. Ctrl+Alt+F puts it back to full width.

Band size and dim level are saved to
%APPDATA%\ReadingRuler\settings.ini and restored next time.


HOW IT WORKS
------------
A single layered, topmost, non-activating window covers the whole virtual
desktop, filled flat black at a uniform alpha. The clear reading band is
not drawn at all - it is a hole cut out of the window region. That means
moving the band costs almost nothing, the colours underneath are entirely
unaltered, and input passes through the band by definition rather than by
a setting that might fail.

Only one copy runs at a time. Starting it twice does nothing, so double-
clicking again by accident will not double the dimming.


BUILDING IT YOURSELF
--------------------
The source is one file, about 400 lines.

  build_msvc.bat    Visual Studio. Run it from a Developer Command Prompt.
  build_mingw.bat   MinGW-w64 or MSYS2.

Nothing beyond the Windows SDK is needed - no third-party libraries.


TUNING
------
The constants at the top of ReadingRuler.cpp control the defaults:

  BAND_DEFAULT   starting band height
  DIM_DEFAULT    0.10 to 0.92
  BAND_STEP      how much Ctrl+Alt+Up/Down moves
  WIDTH_STEP     how much Ctrl+Alt+Left/Right moves
  WIDTH_MIN      narrowest the band can go
  EDGE_LINES     the faint hairlines at the band edges
  POLL_MS        cursor sampling interval

Delete %APPDATA%\ReadingRuler\settings.ini to go back to defaults.


KNOWN LIMITS
------------
Will not draw over a UAC prompt, the lock screen, or a true full-screen
exclusive application. Windows blocks that by design; borderless full
screen is fine.

On a multi-monitor setup all screens dim together, since the overlay
spans the whole virtual desktop as one surface.

MIT licensed. (c) 2026 Tanni Monroe
