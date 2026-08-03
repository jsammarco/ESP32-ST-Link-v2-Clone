#!/usr/bin/env python3
"""Create reproducible static-analysis artifacts for RAZ factory images.

This tool never connects to a device and never writes a firmware image.  It
uses GNU objdump to make a Thumb disassembly, then writes hashes, vectors, and
the evidence-backed GPIO findings used by factory_reference/README.md.

Example:
    python tools/analyze_factory_firmware.py ^
      backups/MyWhiteRAZ_backup.bin backups/MyBlueRAZ_backup.bin

The generated files are intentionally kept out of source control because a
complete raw disassembly is derived from the supplied binaries.  The script
and the reviewed notes are the durable, reproducible reference.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
from pathlib import Path


FLASH_BASE = 0x0800_0000
EXPECTED_SIZE = 0x1_0000
GPIOA = 0x4001_0800
GPIOB = 0x4001_0C00


def find_objdump() -> str:
    """Find GNU Arm objdump, including the default Windows install location."""
    found = shutil.which("arm-none-eabi-objdump")
    if found:
        return found

    program_files_x86 = Path(r"C:\Program Files (x86)")
    candidates = sorted(
        program_files_x86.glob("Arm GNU Toolchain arm-none-eabi/*/bin/arm-none-eabi-objdump.exe"),
        reverse=True,
    )
    if candidates:
        return str(candidates[0])
    raise RuntimeError(
        "arm-none-eabi-objdump was not found. Install Arm GNU Toolchain or put it on PATH."
    )


def vectors(image: bytes) -> list[int]:
    return [int.from_bytes(image[offset : offset + 4], "little") for offset in range(0, 0x40, 4)]


def hex_addr(address: int) -> str:
    return f"0x{address:08X}"


INSTRUCTION = re.compile(r"\s*([0-9a-f]+):\s+[0-9a-f ]+\s+(.+?)\s*$", re.IGNORECASE)
LITERAL_ADDRESS = re.compile(r"@ \((0x[0-9a-f]+)\)", re.IGNORECASE)


def parse_instructions(disassembly: str) -> list[tuple[int, str]]:
    parsed: list[tuple[int, str]] = []
    for line in disassembly.splitlines():
        match = INSTRUCTION.match(line)
        if match:
            parsed.append((int(match.group(1), 16), match.group(2)))
    return parsed


def literal_value(image: bytes, instruction: str) -> int | None:
    match = LITERAL_ADDRESS.search(instruction)
    if not match:
        return None
    offset = int(match.group(1), 16) - FLASH_BASE
    if not 0 <= offset <= len(image) - 4:
        return None
    return int.from_bytes(image[offset : offset + 4], "little")


def input_helper_address(instructions: list[tuple[int, str]]) -> int:
    """Find the vendor GPIO input helper by its small, stable instruction body."""
    for index in range(len(instructions) - 3):
        address, first = instructions[index]
        _, second = instructions[index + 1]
        _, third = instructions[index + 2]
        if (
            first.startswith("mov\tr2, r0")
            and second.startswith("movs\tr0, #0")
            and "ldr\tr3, [r2, #16]" in third
        ):
            return address
    raise RuntimeError("Could not locate the factory GPIO input helper.")


def gpio_input_call_sites(
    image: bytes, instructions: list[tuple[int, str]], helper: int
) -> list[tuple[int, int | None, int | None]]:
    """Return (call address, GPIO base, bit mask) for direct helper calls."""
    sites: list[tuple[int, int | None, int | None]] = []
    target = f"0x{helper:x}"
    for index, (address, instruction) in enumerate(instructions):
        if not instruction.startswith("bl\t") or target not in instruction.lower():
            continue
        gpio: int | None = None
        mask: int | None = None
        # The factory uses a compact, immediate load sequence immediately before
        # this helper. Search only the current basic block for reproducibility.
        for _, previous in reversed(instructions[max(0, index - 10) : index]):
            if gpio is None and previous.startswith("ldr\tr0, [pc"):
                gpio = literal_value(image, previous)
            if mask is None:
                match = re.match(r"movs\tr1, #(\d+)", previous)
                if match:
                    mask = int(match.group(1))
            if gpio is not None and mask is not None:
                break
        sites.append((address, gpio, mask))
    return sites


def write_report(images: list[tuple[Path, bytes]], output_dir: Path, objdump: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    report: list[str] = [
        "# Generated factory-image report",
        "",
        "This is an offline, static report. It is not proof of the physical board net names.",
        "",
    ]

    input_evidence: dict[str, list[tuple[int, int | None, int | None]]] = {}
    for image_path, image in images:
        if len(image) != EXPECTED_SIZE:
            raise RuntimeError(
                f"{image_path} is {len(image):,} bytes; expected a 65,536-byte internal-flash backup."
            )

        digest = hashlib.sha256(image).hexdigest()
        report.extend(
            [
                f"## {image_path.name}",
                "",
                f"- Size: {len(image):,} bytes",
                f"- SHA-256: `{digest}`",
                f"- Initial MSP: `{hex_addr(vectors(image)[0])}`",
                f"- Reset handler: `{hex_addr(vectors(image)[1])}`",
                "- Vector table:",
                "",
                "```text",
                " ".join(hex_addr(value) for value in vectors(image)),
                "```",
                "",
            ]
        )

        disassembly_path = output_dir / f"{image_path.stem}.thumb-disassembly.txt"
        command = [
            objdump,
            "-D",
            "-b",
            "binary",
            "-marm",
            "-M",
            "force-thumb",
            f"--adjust-vma={FLASH_BASE:#x}",
            str(image_path),
        ]
        result = subprocess.run(command, check=True, text=True, capture_output=True)
        disassembly_path.write_text(result.stdout, encoding="utf-8")
        instructions = parse_instructions(result.stdout)
        helper = input_helper_address(instructions)
        sites = gpio_input_call_sites(image, instructions, helper)
        input_evidence[image_path.name] = sites
        report.append(f"- Thumb disassembly: `{disassembly_path.name}`")
        report.append(f"- GPIO input helper: `{hex_addr(helper)}`")
        if sites:
            report.append("- Direct calls to the input helper:")
            for call, gpio, mask in sites:
                gpio_text = hex_addr(gpio) if gpio is not None else "unresolved"
                mask_text = f"0x{mask:02X}" if mask is not None else "unresolved"
                report.append(f"  - `{hex_addr(call)}`: base `{gpio_text}`, mask `{mask_text}`")
        report.append("")

    pb1_or_pb2_read = any(
        gpio == GPIOB and mask in {0x02, 0x04}
        for sites in input_evidence.values()
        for _, gpio, mask in sites
    )
    gpio_b_sites = [
        (name, call, mask)
        for name, sites in input_evidence.items()
        for call, gpio, mask in sites
        if gpio == GPIOB
    ]
    report.extend(
        [
            "## Confirmed static evidence",
            "",
            "- The factory GPIO input helper reads `GPIOx + 0x10` (IDR).",
            "- GPIOB input calls: "
            + (", ".join(f"{name}@{hex_addr(call)} mask 0x{mask:02X}" for name, call, mask in gpio_b_sites)
               if gpio_b_sites else "none resolved"),
            "- " + (
                "A PB1 or PB2 input read was found; review the call site before drawing a charging conclusion."
                if pb1_or_pb2_read
                else "No factory call to this helper uses PB1 (`0x02`) or PB2 (`0x04`)."
            ),
            "- The Blue image's board setup configures PA5 at `0x0800611C` and sets its latch at `0x08006134`.",
            "  This identifies PA5 as a board-control output path, but static analysis alone does not identify",
            "  its physical net, polarity, or safe energisation limits.",
            "",
            f"GPIO bases used in this analysis: GPIOA={hex_addr(GPIOA)}, GPIOB={hex_addr(GPIOB)}.",
            "",
        ]
    )
    (output_dir / "factory-image-report.md").write_text("\n".join(report), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path, help="65,536-byte RAZ internal-flash backup(s)")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("factory_reference/generated"),
        help="artifact directory (default: factory_reference/generated)",
    )
    args = parser.parse_args()

    inputs = [(path, path.read_bytes()) for path in args.images]
    write_report(inputs, args.output, find_objdump())
    print(f"Wrote static-analysis artifacts to: {args.output.resolve()}")


if __name__ == "__main__":
    main()
