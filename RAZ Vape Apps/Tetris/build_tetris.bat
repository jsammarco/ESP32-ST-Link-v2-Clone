@echo off
setlocal
cd /d "%~dp0"

set APP_NAME=tetris
set GCC="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-gcc.exe"
set OBJCOPY="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-objcopy.exe"
set SIZE="C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-size.exe"
if not defined VAPORWARE for %%I in ("%~dp0..\..\..\Vaporware\src") do set "VAPORWARE=%%~fI"

set CPU=-mcpu=cortex-m0 -mthumb
set INC=-I"%VAPORWARE%\include" -I"..\Launcher\src"
set CFLAGS=%CPU% %INC% -Os -ffunction-sections -fdata-sections -Wall -Wextra -std=c11

if not exist build mkdir build

%GCC% %CPU% -x assembler-with-cpp -c "%VAPORWARE%\src\startup.s" -o build\startup.o || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\system.c"  -o build\system.o  || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\display.c" -o build\display.o || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\vape.c"    -o build\vape.o    || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\button.c"  -o build\button.o  || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\battery.c" -o build\battery.o || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\nv.c"      -o build\nv.o      || goto :error
%GCC% %CFLAGS% -c "%VAPORWARE%\src\app.c"     -o build\app.o     || goto :error
%GCC% %CFLAGS% -c "..\Launcher\src\draw_sensor.c" -o build\draw_sensor.o || goto :error
%GCC% %CFLAGS% -c src\main.c                 -o build\main.o    || goto :error

%GCC% %CPU% -T"%VAPORWARE%\n32g031.ld" -Wl,--gc-sections -Wl,-Map=build\%APP_NAME%.map -nostdlib -lnosys ^
  build\startup.o build\system.o build\display.o build\vape.o ^
  build\button.o build\battery.o build\nv.o build\app.o build\draw_sensor.o build\main.o ^
  -o build\%APP_NAME%.elf || goto :error

%OBJCOPY% -O binary build\%APP_NAME%.elf build\%APP_NAME%.bin || goto :error
%OBJCOPY% -O ihex build\%APP_NAME%.elf build\%APP_NAME%.hex || goto :error
%SIZE% build\%APP_NAME%.elf
echo Build SUCCESS: build\%APP_NAME%.bin
endlocal
exit /b 0

:error
echo BUILD FAILED
endlocal
exit /b 1
