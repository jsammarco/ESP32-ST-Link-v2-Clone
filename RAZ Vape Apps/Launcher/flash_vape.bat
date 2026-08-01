@echo off
setlocal
cd /d "%~dp0"

set STLINK_BUSID=3-1
set "USBIPD=%ProgramFiles%\usbipd-win\usbipd.exe"

if not exist "build\launcher.bin" (
    echo ERROR: build\launcher.bin not found. Run build_launcher.bat first.
    exit /b 1
)

echo [1/3] Preparing flash script...
python gen_direct_flash.py || goto :error

echo [2/3] Attaching ST-Link %STLINK_BUSID% to WSL...
if not exist "%USBIPD%" goto :usbipd_error
wsl.exe --cd / /bin/true >nul 2>&1 || goto :wsl_error
for /f "delims=" %%I in ('wsl.exe --cd / sh -lc "wslpath -a -- '%~dp0'"') do set "WSL_APP_DIR=%%I"
if not defined WSL_APP_DIR goto :wsl_path_error

"%USBIPD%" list | %SystemRoot%\System32\findstr.exe /r /c:"^%STLINK_BUSID% .*Attached" >nul
if errorlevel 1 (
    "%USBIPD%" attach --wsl --busid %STLINK_BUSID% >nul 2>&1 || goto :attach_error
) else (
    echo ST-Link is already attached to WSL; continuing.
)

echo [3/3] Flashing Launcher...
wsl.exe --cd "%WSL_APP_DIR%" openocd -f n32g031.openocd.cfg -c "tcl_port disabled; telnet_port disabled; gdb_port disabled" -c "init" -c "source direct_flash.tcl" -c "exit"
if errorlevel 1 goto :error

echo.
echo Flash complete. The firmware was reset and started.
endlocal
exit /b 0

:wsl_error
echo ERROR: WSL is not available. Install WSL and OpenOCD first.
endlocal
exit /b 1

:wsl_path_error
echo ERROR: Could not translate this app folder into a WSL path.
endlocal
exit /b 1

:usbipd_error
echo ERROR: usbipd-win was not found at "%USBIPD%".
endlocal
exit /b 1

:attach_error
echo ERROR: Could not attach ST-Link %STLINK_BUSID%. Check usbipd list and update STLINK_BUSID.
endlocal
exit /b 1

:error
echo ERROR: Flash failed. Confirm SWD wiring and that no other OpenOCD process is using the ST-Link.
endlocal
exit /b 1

