#ifndef RAZ_PIN_CONFIG_H
#define RAZ_PIN_CONFIG_H

/*
 * GPIOs wired through the two series resistors to the USB-C breakout.
 * Override either value with PlatformIO build_flags when using another ESP32
 * board. The SWD programmer and runtime UART intentionally share these pins.
 */
#ifndef RAZ_CC1_GPIO
#define RAZ_CC1_GPIO 25
#endif

#ifndef RAZ_CC2_GPIO
#define RAZ_CC2_GPIO 26
#endif

#if RAZ_CC1_GPIO == RAZ_CC2_GPIO
#error "RAZ_CC1_GPIO and RAZ_CC2_GPIO must be different pins"
#endif

#endif
