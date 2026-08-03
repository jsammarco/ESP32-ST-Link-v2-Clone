#ifndef LAUNCHER_PACMAN_H
#define LAUNCHER_PACMAN_H

#include <stdint.h>

void pacman_init(void);
void pacman_update(uint32_t frame);
void pacman_wake(void);

#endif /* LAUNCHER_PACMAN_H */
