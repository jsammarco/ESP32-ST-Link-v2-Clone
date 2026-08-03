#ifndef LAUNCHER_TETRIS_H
#define LAUNCHER_TETRIS_H

#include <stdint.h>

void tetris_init(void);
void tetris_update(uint32_t frame);
void tetris_wake(void);

#endif /* LAUNCHER_TETRIS_H */
