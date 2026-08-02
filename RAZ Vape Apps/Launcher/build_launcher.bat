@echo off
setlocal
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
if not defined VAPORWARE set VAPORWARE=%~dp0..\..\src

set CPU=-mcpu=cortex-m0 -mthumb
set INC=-I%VAPORWARE%\include -Isrc -Igenerated
set CFLAGS=%CPU% %INC% -Os -ffunction-sections -fdata-sections -Wall -Wextra -std=c11

if not exist generated mkdir generated
if not exist build mkdir build

echo [0/15] Preparing factory vape seed...
%PYTHON% make_vape_seed.py || goto :error

echo [1/15] Converting photos...
if defined LAUNCHER_PHOTOS (
  %PYTHON% convert_images.py --input "%LAUNCHER_PHOTOS%" || goto :error
) else if exist generated\photos.h (
  echo Using existing generated\photos.h. Set LAUNCHER_PHOTOS to rebuild it from source images.
) else (
  %PYTHON% convert_images.py || goto :error
)

echo [2/15] startup.s  (vaporware)
%GCC% %CPU% -x assembler-with-cpp -c %VAPORWARE%\src\startup.s -o build\startup.o || goto :error

echo [3/15] system.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\system.c  -o build\system.o  || goto :error

echo [4/15] display.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\display.c -o build\display.o || goto :error

echo [5/15] vape.c     (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\vape.c    -o build\vape.o    || goto :error

echo [6/15] button.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\button.c  -o build\button.o  || goto :error

echo [7/15] battery.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\battery.c -o build\battery.o || goto :error

echo [8/15] nv.c       (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\nv.c      -o build\nv.o      || goto :error

echo [9/15] app.c      (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\app.c     -o build\app.o     || goto :error

echo [10/15] slideshow.c (Launcher)
%GCC% %CFLAGS% -c src\slideshow.c -o build\slideshow.o || goto :error

echo [11/15] flappy_embedded.c (Launcher)
%GCC% %CFLAGS% -c src\flappy_embedded.c -o build\flappy_embedded.o || goto :error

echo [12/15] draw_sensor.c (Launcher)
%GCC% %CFLAGS% -c src\draw_sensor.c -o build\draw_sensor.o || goto :error

echo [13/15] vape_level.c (Launcher)
%GCC% %CFLAGS% -c src\vape_level.c -o build\vape_level.o || goto :error

echo [14/15] main.c    (Launcher)
%GCC% %CFLAGS% -c src\main.c -o build\main.o || goto :error

echo [15/15] Linking...
%GCC% %CPU% -T%VAPORWARE%\n32g031.ld -Wl,--gc-sections -Wl,-Map=build\%APP_NAME%.map -nostdlib -lnosys ^
  build\startup.o build\system.o build\display.o build\vape.o ^
  build\button.o build\battery.o build\nv.o build\app.o ^
  build\slideshow.o build\flappy_embedded.o build\draw_sensor.o build\vape_level.o build\main.o -o build\%APP_NAME%.elf || goto :error

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
