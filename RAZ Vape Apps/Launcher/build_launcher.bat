@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

if defined LAUNCHER_APP_NAME (
  set APP_NAME=%LAUNCHER_APP_NAME%
) else (
  set APP_NAME=launcher
)
set PYTHON=python
set GCC="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-gcc.exe"
set OBJCOPY="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-objcopy.exe"
set SIZE="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-size.exe"
if not defined VAPORWARE for %%I in ("%~dp0..\..\..\Vaporware\src") do set "VAPORWARE=%%~fI"

if not defined LAUNCHER_APP_1 set LAUNCHER_APP_1=Tetris
if not defined LAUNCHER_APP_2 set LAUNCHER_APP_2=Flappy

set CPU=-mcpu=cortex-m0 -mthumb
set INC=-I%VAPORWARE%\include -Igenerated -Isrc
set CFLAGS=%CPU% %INC% -Os -ffunction-sections -fdata-sections -Wall -Wextra -std=c11
set STREAM_OBJECTS=
set STREAM_LINK_FLAGS=
if "%SCREEN_STREAMER%"=="1" (
  echo [stream] Enabling the native 128x160 SWD screen stream...
  set CFLAGS=!CFLAGS! -DSCREEN_STREAMER=1
  set STREAM_OBJECTS=build\screen_stream.o
  set STREAM_LINK_FLAGS=-Wl,--wrap=display_fill -Wl,--wrap=display_fill_rect -Wl,--wrap=display_draw_image -Wl,--wrap=display_draw_sprite -Wl,--wrap=display_draw_chunk_cpu -Wl,--wrap=display_draw_chunk_dma -Wl,--wrap=display_draw_chunk_2x -Wl,--wrap=display_draw_pixel
)

if not exist generated mkdir generated
if not exist build mkdir build

echo [config] Selecting bundled apps...
%PYTHON% configure_launcher.py --apps "%LAUNCHER_APP_1%" "%LAUNCHER_APP_2%" || goto :error
call generated\launcher_build_config.bat || goto :error

echo [0/12] Preparing factory vape seed...
%PYTHON% make_vape_seed.py || goto :error

if "%LAUNCHER_BUILD_SLIDESHOW%"=="1" (
  echo [photos] Preparing embedded Slideshow photos...
  if defined LAUNCHER_PHOTOS (
    %PYTHON% convert_images.py --input "%LAUNCHER_PHOTOS%" || goto :error
  ) else if exist generated\photos.h (
    echo Using existing generated\photos.h. Select photos in RAZ Manager to replace them.
  ) else (
    echo ERROR: Slideshow was selected, but no embedded photos are available.
    echo Select one to three photos in RAZ Manager and try again.
    goto :error
  )
)

echo [1/12] startup.s  (vaporware)
%GCC% %CPU% -x assembler-with-cpp -c %VAPORWARE%\src\startup.s -o build\startup.o || goto :error

echo [2/12] system.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\system.c  -o build\system.o  || goto :error

echo [3/12] display.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\display.c -o build\display.o || goto :error

echo [4/12] vape.c     (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\vape.c    -o build\vape.o    || goto :error

echo [5/12] button.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\button.c  -o build\button.o  || goto :error

echo [6/12] battery.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\battery.c -o build\battery.o || goto :error

echo [7/12] nv.c       (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\nv.c      -o build\nv.o      || goto :error

echo [8/12] app.c      (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\app.c     -o build\app.o     || goto :error

if "%SCREEN_STREAMER%"=="1" (
  echo [stream] screen_stream.c
  %GCC% %CFLAGS% -c "..\ScreenStreamer\screen_stream.c" -o build\screen_stream.o || goto :error
)

echo [sensor] draw_sensor.c
%GCC% %CFLAGS% -c src\draw_sensor.c -o build\draw_sensor.o || goto :error
set MODULE_OBJECTS=build\draw_sensor.o
if "%LAUNCHER_BUILD_TETRIS%"=="1" (
  echo [module] tetris.c
  %GCC% %CFLAGS% -c src\tetris.c -o build\tetris.o || goto :error
  set MODULE_OBJECTS=!MODULE_OBJECTS! build\tetris.o
)
if "%LAUNCHER_BUILD_PACMAN%"=="1" (
  echo [module] pacman.c
  %GCC% %CFLAGS% -c src\pacman.c -o build\pacman.o || goto :error
  set MODULE_OBJECTS=!MODULE_OBJECTS! build\pacman.o
)
if "%LAUNCHER_BUILD_FLAPPY%"=="1" (
  echo [module] flappy_embedded.c
  %GCC% %CFLAGS% -c src\flappy_embedded.c -o build\flappy_embedded.o || goto :error
  set MODULE_OBJECTS=!MODULE_OBJECTS! build\flappy_embedded.o
)
if "%LAUNCHER_BUILD_SLIDESHOW%"=="1" (
  echo [module] slideshow.c
  %GCC% %CFLAGS% -c src\slideshow.c -o build\slideshow.o || goto :error
  set MODULE_OBJECTS=!MODULE_OBJECTS! build\slideshow.o
)

echo [9/12] vape_level.c (Launcher)
%GCC% %CFLAGS% -c src\vape_level.c -o build\vape_level.o || goto :error

echo [10/12] main.c    (Launcher)
%GCC% %CFLAGS% -c src\main.c -o build\main.o || goto :error

echo [link] Linking...
%GCC% %CPU% -T%VAPORWARE%\n32g031.ld -Wl,--gc-sections !STREAM_LINK_FLAGS! -Wl,-Map=build\%APP_NAME%.map -nostdlib -lnosys ^
  build\startup.o build\system.o build\display.o build\vape.o ^
  build\button.o build\battery.o build\nv.o build\app.o ^
  !MODULE_OBJECTS! !STREAM_OBJECTS! build\vape_level.o build\main.o -o build\%APP_NAME%.elf || goto :error

%OBJCOPY% -O binary build\%APP_NAME%.elf build\%APP_NAME%.bin || goto :error
%OBJCOPY% -O ihex   build\%APP_NAME%.elf build\%APP_NAME%.hex || goto :error
%SIZE% build\%APP_NAME%.elf

echo.
echo Build SUCCESS: build\%APP_NAME%.bin
endlocal
exit /b 0

:error
echo.
echo BUILD FAILED
endlocal
exit /b 1
