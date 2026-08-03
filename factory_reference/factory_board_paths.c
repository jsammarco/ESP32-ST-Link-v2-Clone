/*
 * PARTIAL FACTORY-FIRMWARE PSEUDOCODE - NOT BUILDABLE / NOT FLASHABLE
 *
 * This is a human-maintained decompilation aid for the two factory backups.
 * Addresses below are from MyBlueRAZ_backup.bin (SHA-256 in README.md).
 * It deliberately omits unknown helper semantics rather than inventing them.
 */

typedef unsigned int u32;

#define GPIOA_BASE 0x40010800u
#define GPIOB_BASE 0x40010C00u

#define PA5_MASK   0x00000020u
#define PA12_MASK  0x00001000u
#define PB4_MASK   0x00000010u
#define PB5_MASK   0x00000020u

/* 0x08002EE4: factory GPIO input helper.
 * It returns whether ((GPIOx->IDR & mask) != 0). */
static int factory_gpio_read_bit(u32 gpio_base, u32 mask);
static void factory_gpio_config(u32 gpio_base, u32 mask, u32 mode);

/* 0x08002EF8 and 0x08002EFC: direct GPIO clear/set helper calls. */
static void factory_gpio_clear(u32 gpio_base, u32 mask);
static void factory_gpio_set(u32 gpio_base, u32 mask);

/* 0x0800611C-0x08006134, reached during factory board setup.
 * The opaque configuration record selects PA5; the exact factory-library
 * mode encoding must be preserved as 0x11 until independently decoded. */
static void factory_prepare_pa5(void)
{
    factory_gpio_config(GPIOA_BASE, PA5_MASK, /* mode */ 0x11u); /* 0x0800611C */
    factory_gpio_set(GPIOA_BASE, PA5_MASK);                     /* 0x08006134 */
}

/* 0x08006F50-0x08006F5E, early application entry. */
static void factory_early_board_state(void)
{
    factory_gpio_clear(GPIOB_BASE, PB4_MASK);  /* 0x08006F54 */
    factory_gpio_set(GPIOA_BASE, PA12_MASK);   /* 0x08006F5E */
}

/* 0x08004CA4: the only observed factory GPIOB input read.
 * It is in a serial/bit-banged transfer loop, not a charge-status path. */
static int factory_serial_sample(void)
{
    return factory_gpio_read_bit(GPIOB_BASE, PB5_MASK);
}

/* No static call to factory_gpio_read_bit(GPIOB_BASE, 0x02) or (..., 0x04)
 * exists in either factory image. In particular, PB1/PB2 cannot be described
 * as factory-proven charge-status inputs from these backups alone. */
