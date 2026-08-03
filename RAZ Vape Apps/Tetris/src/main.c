/* Compile Launcher's Tetris module as a standalone app. */
#define tetris_init   app_init
#define tetris_update app_update
#define tetris_wake   app_wake

#include "../../Launcher/src/tetris.c"
