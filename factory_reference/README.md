# Factory firmware reference

This directory documents the two factory N32G031 internal-flash images:

| Image | SHA-256 |
| --- | --- |
| `backups/MyWhiteRAZ_backup.bin` | `b79f60963717edb6095f7399e77e4ae8bddc18c2a991d9afe76bf58e487418b9` |
| `backups/MyBlueRAZ_backup.bin` | `d2b349becabc85d4b302e697497c94a67125dd8b88944fe6e780dd5a7134d6d9` |

They are 64 KiB MCU internal-flash backups. They do not include the external
SPI-flash use counter or any non-MCU charging hardware state.

## Reproducible disassembly

The repo keeps the analysis script and reviewed notes, rather than committing
two large derived disassemblies. Generate a complete Thumb listing at any time:

```powershell
cd "C:\Users\Joe\Projects\ESP32-ST-Link-v2-Clone"
python .\tools\analyze_factory_firmware.py `
  .\backups\MyWhiteRAZ_backup.bin `
  .\backups\MyBlueRAZ_backup.bin
```

This writes read-only derived artifacts under `factory_reference\generated\`:

- `*.thumb-disassembly.txt` - complete raw Thumb disassembly
- `factory-image-report.md` - hashes, vectors, and the static findings

The output directory is ignored by Git; it is safe to regenerate or delete.

## Charging findings

The factory images contain no verified MCU charge-control loop or charge-status
read:

- Both images configure PB1 and PB2 as ordinary GPIO inputs during startup;
  they are never configured as outputs or written by the factory GPIO set/reset
  helpers. The final input configuration has no MCU pull resistor enabled.

- Factory input helper `0x08002EE4` reads `GPIOx + 0x10` (the GPIO input-data
  register).
- Its only GPIOB call site is `0x08004CA4`, which tests PB5 (`mask 0x20`) as
  part of a serial/bit-banged transfer path.
- There are no factory calls to that helper for PB1 (`0x02`) or PB2 (`0x04`).

This supports the important distinction: the MCU can see a cable/power-related
signal on a board pin, but that is not proof that the Li-ion cell is actively
charging. The actual charge regulation is expected to be an external power
path. Therefore the Launcher should describe PB1 as **USB present** unless a
board measurement establishes that it truly reports charge current/state.

The factory app may still behave better on USB because it reduces its load,
enters a low-power state, or configures a board power-enable signal. The static
images alone do not identify a charger-enable net. Do not toggle unknown pins
to search for it: the same signals can be power-hold, display, or heater paths.

## Coil / high-current findings

The factory setup contains a clear PA5 board-control path:

```text
Blue factory image
0x0800611C  prepares GPIOA mask 0x20 (PA5)
0x08006134  writes mask 0x20 through the GPIO set helper
```

That is sufficient to mark PA5 as safety-relevant, but it does **not** prove
that PA5 is the coil gate on every RAZ board, nor does it recover the factory
coil polarity, PWM/timing, cutoff, or fault protection. The two images are
factory build variants and should not be treated as a safe electrical spec.

Any future runtime work should first take a **read-only** SWD register snapshot
while the factory firmware is idle, while it is attempting to vape, and while
USB is attached. Compare GPIO mode/output registers and timer registers across
those snapshots. Do not drive candidate pins during this investigation.

## Reference pseudocode

`factory_board_paths.c` is a deliberately partial, non-buildable pseudocode
reference. Every statement carries an image address and is limited to what the
static trace actually establishes.
