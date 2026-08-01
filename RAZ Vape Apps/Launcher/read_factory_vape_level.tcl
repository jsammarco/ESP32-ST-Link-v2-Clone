# Read the original MyBlueRAZ remaining-vape record from external SPI flash.
#
# This is deliberately read-only.  The factory firmware stores the record at
# 0x003FF000 as four little-endian centisecond ticks followed by 0xBB.  It
# uses PA8=CS, PA9=SCK, PA10=MISO and PA11=MOSI in SPI mode 0.

set RCC_APB2ENR  0x40021018
set GPIOA_MODER  0x40010800
set GPIOA_OTYPER 0x40010804
set GPIOA_PUPDR  0x4001080C
set GPIOA_IDR    0x40010810
set GPIOA_ODR    0x40010814
set GPIOA_BSRR   0x40010818

set FLASH_CS     8
set FLASH_SCK    9
set FLASH_MISO   10
set FLASH_MOSI   11

proc flash_set_pin {pin high} {
    global GPIOA_BSRR
    if {$high} {
        mww $GPIOA_BSRR [expr {1 << $pin}]
    } else {
        mww $GPIOA_BSRR [expr {1 << ($pin + 16)}]
    }
}

proc flash_write_bit {bit} {
    global FLASH_MOSI FLASH_SCK
    flash_set_pin $FLASH_MOSI $bit
    flash_set_pin $FLASH_SCK 1
    flash_set_pin $FLASH_SCK 0
}

proc flash_write_byte {value} {
    for {set bit 7} {$bit >= 0} {incr bit -1} {
        flash_write_bit [expr {($value >> $bit) & 1}]
    }
}

proc flash_read_byte {} {
    global GPIOA_IDR FLASH_MOSI FLASH_SCK FLASH_MISO
    set value 0
    flash_set_pin $FLASH_MOSI 0
    for {set bit 0} {$bit < 8} {incr bit} {
        flash_set_pin $FLASH_SCK 1
        set idr [mrw $GPIOA_IDR]
        set value [expr {($value << 1) | (($idr >> $FLASH_MISO) & 1)}]
        flash_set_pin $FLASH_SCK 0
    }
    return $value
}

catch {halt}
sleep 20

# Keep the hardware watchdog from resetting the stopped MCU during the read.
mww 0x40003000 0x0000AAAA
mww 0x40003000 0x00005555
mww 0x40003004 0x00000006
mww 0x40003008 0x00000FFF
mww 0x40003000 0x0000AAAA

# Enable GPIOA.  Drive CS high / clock low before taking the data pins over.
set apb2enr [mrw $RCC_APB2ENR]
mww $RCC_APB2ENR [expr {$apb2enr | (1 << 2)}]
flash_set_pin $FLASH_CS 1
flash_set_pin $FLASH_SCK 0
flash_set_pin $FLASH_MOSI 0

# PA8, PA9 and PA11 are push-pull outputs; PA10 is a pulled-up input.
set moder [mrw $GPIOA_MODER]
set otyper [mrw $GPIOA_OTYPER]
set pupdr [mrw $GPIOA_PUPDR]
set odr [mrw $GPIOA_ODR]
mww $GPIOA_MODER  [expr {($moder & ~0x00FF0000) | (1 << 16) | (1 << 18) | (1 << 22)}]
mww $GPIOA_OTYPER [expr {$otyper & ~0x00000B00}]
mww $GPIOA_PUPDR  [expr {($pupdr & ~0x00FF0000) | (1 << 20)}]

# Confirm that the flash chip responds before interpreting the counter bytes.
flash_set_pin $FLASH_CS 0
flash_write_byte 0x9F
set jedec0 [flash_read_byte]
set jedec1 [flash_read_byte]
set jedec2 [flash_read_byte]
flash_set_pin $FLASH_CS 1

# SPI READ (0x03), followed by the 24-bit factory record address 0x3FF000.
flash_set_pin $FLASH_CS 0
flash_write_byte 0x03
flash_write_byte 0x3F
flash_write_byte 0xF0
flash_write_byte 0x00
set byte0 [flash_read_byte]
set byte1 [flash_read_byte]
set byte2 [flash_read_byte]
set byte3 [flash_read_byte]
set marker [flash_read_byte]
flash_set_pin $FLASH_CS 1

set ticks [expr {$byte0 | ($byte1 << 8) | ($byte2 << 16) | ($byte3 << 24)}]
puts ""
puts [format "External SPI flash ID: %02X %02X %02X" $jedec0 $jedec1 $jedec2]
puts [format "Factory record: %02X %02X %02X %02X %02X" $byte0 $byte1 $byte2 $byte3 $marker]
if {$jedec0 == 0xFF && $jedec1 == 0xFF && $jedec2 == 0xFF} {
    puts "The external flash did not respond (all-FF ID); do not use this record."
    puts "Check the SPI pin mapping/power before trying to import a value."
} elseif {$marker != 0xBB} {
    puts [format "Factory record is not valid (marker 0x%02X, expected 0xBB)." $marker]
    puts "The original firmware treats this as a fresh 100% level."
    puts "FACTORY_VAPE_TICKS=0"
    puts "FACTORY_VAPE_PERCENT=100"
    puts "FACTORY_VAPE_BARS=6/6"
} else {
    if {$ticks >= 340000} {
        set used 340000
        set bars 0
    } else {
        set used $ticks
        set bars [expr {6 - ($used / 60000)}]
    }
    set percent [expr {(($used * -1 + 340000) * 100) / 340000}]
    puts [format "FACTORY_VAPE_TICKS=%u" $ticks]
    puts [format "FACTORY_VAPE_PERCENT=%u" $percent]
    puts [format "FACTORY_VAPE_BARS=%u/6" $bars]
}

# Restore the MCU state captured while it was halted, then resume the Launcher.
# This intentionally does not write to the external SPI flash or reset firmware.
set flash_output_mask 0x00000B00
mww $GPIOA_BSRR [expr {($odr & $flash_output_mask) | (((~$odr) & $flash_output_mask) << 16)}]
mww $GPIOA_PUPDR  $pupdr
mww $GPIOA_OTYPER $otyper
mww $GPIOA_MODER  $moder
mww $RCC_APB2ENR  $apb2enr
resume
