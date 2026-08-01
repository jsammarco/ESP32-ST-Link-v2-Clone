#ifndef LAUNCHER_SLIDESHOW_H
#define LAUNCHER_SLIDESHOW_H

#include <stdint.h>

void slideshow_init(void);
/* Returns 1 when a triple-tap asks the launcher to return to the menu. */
uint8_t slideshow_update(uint32_t frame);
void slideshow_wake(void);

#endif /* LAUNCHER_SLIDESHOW_H */

