/* Compile the existing Flappy source as a Launcher module without changing
 * examples/flappy.  Its state and rendering remain exactly the same. */
#include "vape_level.h"

#define app_init   flappy_module_init
#define app_update flappy_module_update
#define app_wake   flappy_module_wake
#define vape_coil_on  vape_level_coil_on
#define vape_coil_off vape_level_coil_off

#include "../../flappy/src/flappy.c"

#undef vape_coil_off
#undef vape_coil_on
