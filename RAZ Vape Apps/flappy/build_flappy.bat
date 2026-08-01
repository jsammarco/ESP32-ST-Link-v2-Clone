@echo off
cd /d "%~dp0"
:: build_flappy.bat - FlappyVape for N32G031 + GC9107 128x160 LCD
:: Uses the full Vaporware SDK framework (app.c, button.c, nv.c, etc.)

set GCC="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-gcc.exe"
set OBJCOPY="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-objcopy.exe"
set SIZE="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-size.exe"

set VAPORWARE=%~dp0..\..\src

set CPU=-mcpu=cortex-m0 -mthumb
set INC=-I%VAPORWARE%\include
set CFLAGS=%CPU% %INC% -Os -ffunction-sections -fdata-sections -Wall -std=c11

if not exist build mkdir build

echo [1/9] startup.s  (vaporware)
%GCC% %CPU% -x assembler-with-cpp -c %VAPORWARE%\src\startup.s -o build\startup.o || goto :error

echo [2/9] system.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\system.c  -o build\system.o  || goto :error

echo [3/9] display.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\display.c -o build\display.o || goto :error

echo [4/9] vape.c     (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\vape.c    -o build\vape.o    || goto :error

echo [5/9] button.c   (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\button.c  -o build\button.o  || goto :error

echo [6/9] battery.c  (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\battery.c -o build\battery.o || goto :error

echo [7/9] nv.c       (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\nv.c      -o build\nv.o      || goto :error

echo [8/9] app.c      (vaporware)
%GCC% %CFLAGS% -c %VAPORWARE%\src\app.c     -o build\app.o     || goto :error

echo [9/9] flappy.c   (app)
%GCC% %CFLAGS% -c src\flappy.c -o build\flappy.o || goto :error

echo Linking...
%GCC% %CPU% -T%VAPORWARE%\n32g031.ld -Wl,--gc-sections -Wl,-Map=build\flappy.map -nostdlib -lnosys ^
  build\startup.o build\system.o build\display.o build\vape.o ^
  build\button.o build\battery.o build\nv.o build\app.o ^
  build\flappy.o ^
  -o build\flappy.elf || goto :error

%OBJCOPY% -O binary build\flappy.elf build\flappy.bin || goto :error
%OBJCOPY% -O ihex   build\flappy.elf build\flappy.hex || goto :error
%SIZE% build\flappy.elf

echo.
echo Build SUCCESS: build\flappy.bin
echo.
echo Next: python gen_direct_flash.py   then   flash_vape.bat
goto :eof

:error
echo BUILD FAILED
exit /b 1
