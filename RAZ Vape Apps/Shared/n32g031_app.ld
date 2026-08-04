/* N32G031 app layout with an enforced 2 KB stack reserve.
 *
 * The SDK linker script documents this split but exposes all 8 KB to .data
 * and .bss, allowing a large Launcher to overwrite its descending stack at
 * runtime.  SRAM_DATA ends at 0x20001800 while _estack remains 0x20002000.
 */
MEMORY
{
    FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 60K
    SRAM_DATA (rwx) : ORIGIN = 0x20000000, LENGTH = 6K
}

ENTRY(Reset_Handler)

_estack = 0x20002000;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .sprites :
    {
        . = ALIGN(4);
        _sprites_start = .;
        *(.sprites)
        *(.sprites*)
        . = ALIGN(4);
        _sprites_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM_DATA AT > FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM_DATA

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        *(.stack)
    } > SRAM_DATA

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }
}
