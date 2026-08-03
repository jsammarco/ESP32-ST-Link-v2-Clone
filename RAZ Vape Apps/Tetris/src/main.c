/* Compile Launcher's Tetris module as a standalone app. */
#include "battery.h"
#include "vape.h"

#define TETRIS_COIL_ON()       vape_coil_on()
#define TETRIS_COIL_OFF()      vape_coil_off()
#define TETRIS_COIL_ALLOWED()  (bat_read_raw() > BAT_CRIT)
#define tetris_init   app_init
#define tetris_update app_update
#define tetris_wake   app_wake

#include "../../Launcher/src/tetris.c"
