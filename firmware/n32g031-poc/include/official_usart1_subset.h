#ifndef RAZ_POC_OFFICIAL_USART1_SUBSET_H
#define RAZ_POC_OFFICIAL_USART1_SUBSET_H

/*
 * Exact USART1 layout and bit names from NationsTech's N32G031 CMSIS device
 * header (Nationstech.N32G031_Library 1.0.4, n32g031.h). This deliberately
 * small subset avoids mixing the full official device header with the proven
 * Vaporware display compatibility header used elsewhere in this target.
 */
#include <stdint.h>

typedef struct {
    volatile uint16_t STS;
    uint16_t RESERVED0;
    volatile uint16_t DAT;
    uint16_t RESERVED1;
    volatile uint16_t BRCF;
    uint16_t RESERVED2;
    volatile uint16_t CTRL1;
    uint16_t RESERVED3;
    volatile uint16_t CTRL2;
    uint16_t RESERVED4;
    volatile uint16_t CTRL3;
    uint16_t RESERVED5;
    volatile uint16_t GTP;
    uint16_t RESERVED6;
} raz_usart_module_t;

#define RAZ_USART1_BASE       0x40013800UL
#define RAZ_USART1            ((raz_usart_module_t *)RAZ_USART1_BASE)

#define RAZ_USART_STS_RXDNE   ((uint16_t)0x0020)
#define RAZ_USART_STS_TXDE    ((uint16_t)0x0080)
#define RAZ_USART_CTRL1_RXEN  ((uint16_t)0x0004)
#define RAZ_USART_CTRL1_TXEN  ((uint16_t)0x0008)
#define RAZ_USART_CTRL1_UEN   ((uint16_t)0x2000)

/* Official USART_Init() result for PCLK2=8 MHz and 9,600 baud. */
#define RAZ_USART_BRCF_8MHZ_9600 ((uint16_t)0x0341)

#endif
