/* Compile Launcher's Pac-Man module as a standalone app. */
#include "vape.h"

#define PACMAN_COIL_OFF() vape_coil_off()
#define pacman_init   app_init
#define pacman_update app_update
#define pacman_wake   app_wake

#include "../../Launcher/src/pacman.c"
