# ESP32 proof-of-concept firmware

This PlatformIO/Arduino project keeps its UART TX GPIO high-impedance until it
has passively received an exact `PING` line from the N32. `RAZ_UART_RX_PIN` and
`RAZ_UART_TX_PIN` are build-time configurable in `platformio.ini`. An optional
active-low `RAZ_LINK_ENABLE_PIN` can add a hardware permission input.

The Wi-Fi radio is normally off. `SCAN` enables station mode, explicitly
disconnects without associating, performs an asynchronous scan, returns the 20
strongest results, frees the scan records, and turns Wi-Fi off again. No network
credentials are configured by this firmware.

Build without uploading:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Do not upload until the voltage, CC continuity, two 1 kOhm series resistors,
and programmer/runtime isolation described in `../../docs/RAZ_ESP32_POC.md`
have been verified.
