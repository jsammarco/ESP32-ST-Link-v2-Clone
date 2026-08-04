# RAZ N32G031 + ESP32 proof of concept

## Status and safety gates

Both firmware projects build, and the N32 protocol parser passes its host test
suite. No hardware image was flashed during this work: the live read-only probe
on `COM7` returned `ERR PROBE` after trying both CC mappings. Do not skip the
fresh probe and backup steps below.

This is bench-only firmware. Do not puff, charge the vape, or connect USB VBUS
while the POC is installed until the exact board's charger/protection ownership
has been confirmed. The code never initializes known charging or battery-sense
registers, but replacing the factory application cannot prove that an unknown
board revision does not expect MCU supervision.

Two facts must be measured on the exact vape and breakout before direct wiring:

1. Measure N32 I/O supply voltage at the target, relative to target GND. The
   existing board SDK says approximately 3.0 V, but that is not a current bench
   measurement. Direct ESP32 3.3 V signaling is allowed only if the target I/O
   voltage and absolute limits have been confirmed. Otherwise use two properly
   powered, unidirectional logic-level translators, one in each direction.
2. Resolve which physical CC contact reaches PA13 and PA14 in the current plug
   orientation. The repository history contains both orientations, so a label
   such as `CC1` is not enough by itself.

Never connect ESP32 `3V3`, `5V`, `VBUS`, `D+`, or `D-` to the vape. The vape and
ESP32 power themselves; connect only signal ground and the two isolated signal
lines after the voltage check.

## Verified silicon and board mapping

The NationsTech N32G031 alternate-function table verifies the MCU-side mapping:

| N32 pin | Reset function | Runtime function | AF |
|---|---|---|---|
| PA13 | SWDIO | USART1_RX | AF4 |
| PA14 | SWCLK | USART1_TX | AF4 |

The inspected, already-used board firmware identifies the remaining POC pins:

| Function | N32 pin | Treatment in this POC |
|---|---|---|
| Button | PA7, active low | Input with pull-up |
| Identified heater/firing gate | PA5 | Preloaded low, output low, forced low continuously; no enable API |
| LCD SPI clock | PB3 / SPI1_SCK AF0 | Existing proven GC9107 driver |
| LCD SPI data | PB5 / SPI1_MOSI AF0 | Existing proven GC9107 driver |
| LCD CS | PA15, active low | Existing proven driver |
| LCD D/C | PB7 | Existing proven driver |
| LCD reset | PB6, active low | Existing proven driver |
| LCD backlight | PB4, active low | Existing proven driver |
| Display control | PA4 and PA6 | Existing driver only drives these low; PA5 is left to the safety module |

The POC does not compile or call the battery ADC, pressure sensor, charging,
sleep, NV, or vape application modules. It does not alter charging or
battery-protection configuration. Wi-Fi credentials exist only in ESP32 RAM;
the runtime explicitly disables persistent Wi-Fi credential storage.

Official references:

- [NationsTech N32G031 product and SDK page](https://www.nationstech.com/product/general/n32g/n32g03x/n32g031/)
- [N32G031 series datasheet](https://www.nationstech.com/uploads/Microcontrollers/N32G031xx_V2.5.0/2-Datasheet/EN_DS_N32G031_Series_Datasheet_V1.5.1.pdf)
- [N32G031 user manual](https://www.nationstech.com/uploads/Microcontrollers/N32G031xx_V2.5.0/3-User_Manual/EN_UM_N32G031_Series_User_Manual_V2.5.0.pdf), especially the GPIO AF tables

## Isolation and wiring

The root ESP32 firmware now contains both the SWD programmer and Wi-Fi runtime,
so the same two wired ESP32 pins can remain connected. The two modes are
mutually exclusive: programmer-idle leaves GPIO25/GPIO26 as floating inputs,
every SWD operation releases them when it finishes, and runtime starts RX-only
with its TX pin as an input until the N32 sends an exact `PING`.

Do not connect a second SWD probe/programmer while the ESP32 is connected to the
CC lines. If an external probe is required, use a break-before-make two-pole
selector or physically disconnect both ESP32 signal wires first; common ground
may remain connected. Software mode selection is not an interlock between two
physical programmers.

Place a 1 kΩ series resistor in each communication line, near the ESP32. The
same resistors remain in circuit in both programmer and runtime modes.

After determining the CC mapping, use exactly one of these tables.

### Mapping A: repository primary/default orientation

This is the root firmware's first probe orientation, but it is not yet verified
on the currently attached hardware.

| N32/runtime net | USB-C breakout | Series part | ESP32 |
|---|---|---|---|
| PA14 / USART1_TX / SWCLK | CC1 | 1 kΩ | GPIO25 (runtime RX) |
| PA13 / USART1_RX / SWDIO | CC2 | 1 kΩ | GPIO26 (runtime TX) |
| GND | GND | direct | GND |

### Mapping B: flipped orientation

| N32/runtime net | USB-C breakout | Series part | ESP32 |
|---|---|---|---|
| PA14 / USART1_TX / SWCLK | CC2 | 1 kΩ | GPIO26 (runtime RX) |
| PA13 / USART1_RX / SWDIO | CC1 | 1 kΩ | GPIO25 (runtime TX) |
| GND | GND | direct | GND |

The integrated ESP32 firmware tries both orientations during a read-only probe,
reports the successful map, stores it in ESP32 NVS, and automatically uses the
corresponding RX/TX orientation in runtime mode. Build/upload it, wake the vape,
then run:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --target upload --upload-port COM7
python .\tools\fast_flash.py --port COM7 --probe
```

Interpret the result using the fixed breakout wiring GPIO25→CC1 and
GPIO26→CC2:

- `MAP1 SWDIO=GPIO26 SWCLK=GPIO25` means Mapping A.
- `MAP2 SWDIO=GPIO25 SWCLK=GPIO26` means Mapping B.

Do not infer a mapping from a failed probe.

## Backup before the first flash

1. Build/upload the integrated root ESP32 firmware and choose **Enable SWD
   programmer** in RAZ Manager. Its target pins remain high-impedance while
   idle.
2. Obtain a successful `IDR` response as shown above.
3. Create a new device-specific backup:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\backup_raz.ps1 `
  -Port COM7 `
  -Destination ".\backups\pre-poc-$(Get-Date -Format yyyyMMdd-HHmmss)"
```

4. Confirm that the new folder contains a 65,536-byte `internal_flash.bin`, an
   8,192-byte `ram_snapshot.bin`, and `manifest.json`. Recompute the flash hash:

```powershell
Get-FileHash -Algorithm SHA256 .\backups\pre-poc-*\internal_flash.bin
```

Five existing backup manifests were checked during development: every internal
flash file was 65,536 bytes and matched its recorded SHA-256. A new backup is
still required for the exact device immediately before flashing.

## Build

### N32G031

The N32 build uses Arm GNU Toolchain 14.2 and the adjacent Vaporware SDK's
proven startup, timebase, N32 compatibility header, and GC9107 driver. The UART
register layout and values are copied from the inspected official NationsTech
CMSIS/standard-peripheral SDK. Battery/vape modules are not linked.

```powershell
cd .\firmware\n32g031-poc
.\build.ps1 -Target all -Clean
.\tests\run_host_tests.ps1
cd ..\..
```

Outputs:

| Image | Purpose |
|---|---|
| `firmware/n32g031-poc/build/raz_minimal_test.bin` | First-flash display/button check; PA13/PA14 stay SWD forever |
| `firmware/n32g031-poc/build/raz_esp32_poc.bin` | Full menu/UART firmware |

Set `VAPORWARE` and `ARM_GCC_BIN` if the defaults do not match the machine.

### Integrated ESP32 programmer/runtime

Build the root project. It contains both operating modes and remembers both the
selected mode and last successful SWD map across ESP32 resets:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

The shared CC/UART pins default to GPIO25 and GPIO26. For another ESP32 board,
set `-D RAZ_CC1_GPIO=<gpio>` and `-D RAZ_CC2_GPIO=<gpio>` under
`build_flags` in `platformio.ini`; the integrated SWD pin maps change with the
same settings, so update the wiring table and use output-capable 3.3 V GPIOs.

RAZ Manager's **Update ESP32 firmware...** button builds and uploads this same
combined image. `firmware/esp32-poc` remains as a standalone reference build;
it is not needed for the recommended procedure.

The Manager's normal **App** selector also contains **RAZ Browser**. Flashing
that entry builds `raz_esp32_poc.bin` fresh, can create the usual pre-flash
backup, and then programs it like the other standalone vape apps. Coil remapping
and the optional SWD screen-stream wrapper are disabled for this entry because
the browser firmware already owns the display and permanently disables the
identified heater output. Select **Enable Wi-Fi runtime** only after the flash
and recovery checks complete.

## Staged flashing and test procedure

Keep the integrated ESP32 in programmer mode for the first three N32 stages.

1. **Minimal N32 test:** after a fresh backup and successful probe, flash:

   ```powershell
   python .\tools\fast_flash.py --port COM7 --flash `
     .\firmware\n32g031-poc\build\raz_minimal_test.bin
   ```

   Confirm `SAFE TEST`, `SWD ACTIVE`, and the button pressed/released state.
   Re-run the SWD probe; it must still succeed because this image never changes
   PA13/PA14.

2. **Recovery behavior:** reset/power-cycle while holding the vape button. Keep
   it held and confirm SWD can be probed after more than two seconds. Release
   and reset normally.

3. **Full N32 image:** while still in programmer mode, flash:

   ```powershell
   python .\tools\fast_flash.py --port COM7 --flash `
     .\firmware\n32g031-poc\build\raz_esp32_poc.bin
   ```

   On a normal reset, SWD must work during the first approximately two seconds.
   Holding the button from reset must retain SWD indefinitely.

   When replacing the current RAZ Browser image, select **SWD Recovery** on the
   vape, long-press once to open it, then long-press again to confirm. The N32
   sends `SWDRECOVERY`; the ESP32 replies `SWD,READY`, persists programmer mode,
   flushes/stops UART, and releases both CC pins before the N32 selects AF0.
   Wait for `SWD ACTIVE`, then confirm **Test SWD connection** reports an `IDR`.
   Browser builds from before this handshake still require a real reset or
   power cycle with the vape button held continuously.

4. **ESP32 runtime:** in RAZ Manager click **Enable Wi-Fi runtime**, or use:

   ```powershell
   python .\tools\fast_flash.py --port COM7 --esp32-runtime
   ```

   Confirm `MODE RUNTIME ...`. The choice persists across resets. After the
   measured voltage/level-translation check and both 1 kΩ resistors, reset the
   N32 normally. The ESP32 TX remains input/high-impedance until it receives an
   exact `PING`; the N32 does not send that until its SWD window has expired.
5. Open **ESP32 Status**. It should change to `ONLINE` after `PONG`. Power off or
   disconnect the ESP32 and verify the menu and button remain responsive and
   status eventually changes to `OFFLINE`.
6. Test tap-next, 1.5-second hold-select, and double-press-back on the menus.
7. Select **Wi-Fi Scan**. Before connection, verify the ESP32 does not associate,
   the radio turns off after the scan, at most 20 APs appear, RSSI is descending,
   and `OPEN` or `SECURE` is shown. Test zero APs and more than 20 visible APs if
   possible.
8. On a network result, hold for 1.5 seconds to connect. Open networks submit
   immediately. Secured networks open the on-screen keyboard: tap moves to the
   next key, double press types/activates the selected key, `PG` changes between
   upper/lower/symbol pages, `SP` inserts a space, `BK` deletes, `OK` submits,
   and `X` cancels. A 1.5-second hold also submits. Verify the status screen shows
    the SSID and DHCP address after connection.
   If scanning fails, use **Link diagnostics** in RAZ Manager. `RX_BYTES=0`
   points to mode/map/wiring or an N32 still in recovery; bytes without `PINGS`
   point to invalid UART framing/noise; `PINGS` without `SCAN_STARTS` means the
   basic link works but the scan command did not arrive; a negative `LAST_SCAN`
   is an ESP32 Wi-Fi API failure code.
9. Open **Browser**, then choose **Hackaday.com**, **Google.com**, or **Custom
    address**. Custom addresses use the same keyboard and begin with `https://`.
    Confirm that a page produces a title plus styled text lines. On the page,
    one click scrolls down one rendered line, double click scrolls up one line,
    and a 1.5-second hold returns to the main menu.
10. Test SSIDs containing commas and unusual bytes. Commas become semicolons;
    CR/LF/control bytes become spaces; non-ASCII bytes become `?`, so no SSID can
    inject a protocol line. Connection uses the ESP32's retained scan-result ID,
    not the sanitized display name.
11. Use **Disconnect** and verify credentials are removed from ESP32 RAM. While
    runtime is selected, do not attach an external programmer. Physically
    isolate both ESP32 signal lines before connecting one.

## Recovery and factory restore

1. For a current Browser build, use the vape's **SWD Recovery** menu item and
   wait for `SWD ACTIVE`. This automatically places the current ESP32 firmware
   in persistent programmer mode. If it reports `ESP32 NO ACK`, manually place
   the ESP32 in programmer mode or physically disconnect it before long-pressing
   the separately labeled force action. Never force while an ESP32 UART output
   may still be driving a shared line.
2. For an older Browser build, in RAZ Manager click **Enable SWD programmer**,
   or run:

   ```powershell
   python .\tools\fast_flash.py --port COM7 --esp32-programmer
   ```

   Confirm `MODE PROGRAMMER ... IDLE=HIGH-Z`.
3. Hold the vape button before reset/power-up and keep holding it. The older POC
   never leaves SWD in this mode. Without the button, connect-under-reset must
   complete inside the approximately two-second window.
4. Probe and restore the verified full backup:

```powershell
python .\tools\fast_flash.py --port COM7 --probe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\restore_raz.ps1 `
  -Port COM7 `
  -Backup ".\backups\pre-poc-YYYYMMDD-HHMMSS" `
  -Confirm
```

The restore tool validates the manifest hash, erases/programs all 64 KB, reads
it back for verification, and resets only after success.

## Protocol and implementation limits

N32→ESP32:

```text
PING
SCAN
WIFI?
JOIN,<scan-index>,<hex-password>
DISCONNECT
GET,HACKADAY
GET,GOOGLE
GETHEX,<hex-url>
SCROLL,<1|-1>
SWDRECOVERY
```

ESP32→N32:

```text
ESP32,READY
PONG
SCAN,STARTED
BEGIN,<count>
AP,<rssi>,<OPEN|SECURE>,<sanitized-ssid>
END
WIFI,CONNECTING,<ssid>
WIFI,CONNECTED,<ssid>,<ip>
WIFI,DISCONNECTED,<reason>
BROWSER,LOADING
VIEW,<top>,<total>,<count>,<truncated>,<title>
TXT,<P|H|L|A|M>,<text>
VIEWEND
SWD,READY
ERROR,<SCAN|WIFI|BROWSER|COMMAND>,<reason>
```

The N32 uses a 128-byte line buffer, fixed storage for 20 APs and a ten-line
viewport, no heap, manual bounded parsing, and explicit scan/Wi-Fi/browser
timeouts. Passwords and custom URLs are hex encoded, length checked, and cleared
from N32/ESP32 command buffers after use. The ESP32 performs an asynchronous scan
and copies the strongest 20 results into fixed storage; the Arduino Wi-Fi/TLS
stacks themselves use dynamic memory internally.

The N32 paints the scan screen before transmitting `SCAN`, so the blocking LCD
driver cannot discard the immediate acknowledgment. The ESP32 replies with
`SCAN,STARTED`; absence of that reply times out in five seconds, while an
acknowledged scan receives fifteen seconds to finish. Main-menu, site-picker,
and same-page keyboard cursor movement repaint only the old and new selections
instead of clearing the entire LCD.

`SWDRECOVERY` is a two-sided break-before-make handoff. The ESP32 transmits
`SWD,READY`, drains its UART, persists programmer mode, and makes CC1/CC2 inputs.
Only after parsing that acknowledgement does the N32 disable USART1 and restore
PA13/PA14 AF0 with the documented SWDIO pull-up and SWCLK pull-down. The final
screen is painted first, the heater remains forced off, and the N32 stays in an
indefinite watchdog-fed service loop. A no-ack force path is intentionally a
separate long press and is safe only with the adapter disconnected or already
confirmed high-impedance.

## Text-browser limits

This is deliberately a text browser, not a general browser engine. The ESP32
streams at most 128 KB of a response into a fixed 160-line document. The parser
handles HTML text, headings, paragraphs, lists, links, common entities, block
layout, and inline or embedded CSS for `display`, `visibility`, `font-weight`,
and `text-transform`. Script, SVG, canvas, iframe, template, images, forms,
JavaScript, video, external CSS, cookies, and downloads are ignored. Google is a
selectable page, but its search form is not interactive.

HTTPS certificate verification is enabled; the firmware does not use an
insecure TLS mode. The compact built-in trust store covers the bundled sites and
several common certificate authorities, so a custom HTTPS site using another
root will return a certificate/connection error. Root certificates expire or
change and must be maintained with firmware updates. Plain `http://` custom URLs
are accepted but are unencrypted. Redirects that change from HTTP to HTTPS may
not work with the constrained client; enter the final HTTPS address directly.

The Manager and `fast_flash.py` use a separate USB-serial control byte for the
integrated ESP32 mode: `M` queries, `W` selects Wi-Fi runtime, and `P` selects
SWD programmer. Replies are `MODE RUNTIME ...` or
`MODE PROGRAMMER ... IDLE=HIGH-Z`. These bytes are never sent over the N32 UART.

## Facts not verified on the bench

- Which CC contact maps to PA13/PA14 in the present plug orientation. The live
  probe was unavailable, so the default remains a configuration choice.
- Target I/O voltage and whether direct 3.3 V signaling is safe on this unit.
- The exact RAZ model/PCB revision and whether another revision has additional
  heater-enable signals. PA5 is the only firing gate identified in the local
  SDK; all unrelated GPIOs are left at reset state, while display PA4/PA6 are
  only driven low.
- Button PA7 and all GC9107 board nets were corroborated by the existing working
  firmware and display captures, but not by a schematic or fresh continuity
  measurement.
- Whether charging/battery protection is entirely external on this board. The
  POC neither initializes nor writes those MCU-side modules/registers.
- Physical isolation-switch behavior, resistor installation, and signal
  integrity on the actual wiring.
- End-to-end hardware behavior. The build and parser tests passed, but the
  attempted read-only live probe returned `ERR PROBE`, so no flash was attempted.
- End-to-end ESP32 TLS, DHCP, HTML/CSS rendering, and page scrolling on the
  physical device. The two built-in sites responded over HTTPS during host-side
  checks, but the combined image has not been uploaded to hardware.
