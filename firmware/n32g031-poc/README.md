# N32G031 proof-of-concept firmware

The application is split into hardware safety initialization, SWD/runtime
switching, USART1, GC9107 display UI, button gestures, a bounded on-screen
keyboard, protocol parsing, and menu/browser state. It deliberately links only the proven `system.c` and
`display.c` portions of the adjacent Vaporware SDK; battery, charging, pressure,
NV, sleep, and vape/coil application modules are not linked.

Build both the full image and the first-flash minimal test:

```powershell
.\build.ps1 -Target all -Clean
```

Set `VAPORWARE` to the absolute `Vaporware\src` path and `ARM_GCC_BIN` to an
Arm GNU Toolchain `bin` directory if they are not at the detected workspace
locations. Outputs are written only under `build\`.

The minimal image never changes PA13/PA14 from SWD. The full image waits two
seconds, honors the boot-button recovery hold, and then selects USART1 AF4:
PA14 is TX and PA13 is RX. Its guarded **SWD Recovery** menu item coordinates
with the ESP32, restores PA13/PA14 to AF0, disables USART, and remains in a
heater-disabled SWD service loop until the next flash/reset.

## One-button browser controls

- Menus: tap moves, double press goes back, and a 1.5-second hold selects.
- Network result: tap cycles results and a 1.5-second hold connects.
- Keyboard: tap moves, double press types/activates a key, and a 1.5-second hold
  submits. `PG`, `SP`, `BK`, `OK`, and `X` mean page, space, backspace, done,
  and cancel.
- Web page: tap scrolls down one line, double press scrolls up one line, and a
  1.5-second hold returns to the main menu.
- SWD Recovery: hold once to select, then hold again to confirm. On
  `SWD,READY`, the screen shows `SWD ACTIVE` and the UART pins become SWD pins.
  If the ESP32 does not acknowledge, put it in programmer mode or disconnect
  it before using the separately labeled force action.

The N32 keeps only ten rendered lines. HTML fetch, CSS processing, TLS, and the
160-line document live on the ESP32. Passwords and URLs use bounded hex-encoded
protocol fields; password storage is explicitly cleared after submission.
