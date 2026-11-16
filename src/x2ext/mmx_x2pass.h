#ifndef MMX_X2PASS_H_
#define MMX_X2PASS_H_
/* X2 Password Saves (SRAM): X2's last password kept in a host file and
 * filled in on X2's password screen (mmx_x2pass.c). Enables the page turn
 * between X1's and X2's password screens (mmx_x2ext.c). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MMX_X2PASS_DIGITS = 16 };
/* The mod is on (offline, with its file open). */
bool MmxX2PassEnabled(void);
/* A password is saved (filled in on X2's page, checked with X1's). */
bool MmxX2PassHasSaved(void);
/* While X2 runs at its stage select: its password for the progress so far,
 * from X2's own generator ($00:F013), saved. WRAM and the CPU are put back
 * as they were. */
void MmxX2PassSaveProgress(void);

#ifdef __cplusplus
}
#endif

#endif /* MMX_X2PASS_H_ */
