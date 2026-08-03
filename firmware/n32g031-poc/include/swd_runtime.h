#ifndef RAZ_POC_SWD_RUNTIME_H
#define RAZ_POC_SWD_RUNTIME_H

/*
 * Leave PA13/PA14 in their reset SWD state for two seconds. If PA7 was held
 * continuously from boot, this function never returns and SWD stays enabled.
 */
void swd_recovery_window(void);

#endif
