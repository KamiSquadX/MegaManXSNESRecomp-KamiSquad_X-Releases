#ifndef MMX_X2PAUSE_H_
#define MMX_X2PAUSE_H_
/* X1 to X2 boss weapons: X2's pause screen pages and X1's X2 weapon locks
 * (mmx_x2pause.c). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool MmxX2PauseEnabled(void);
/* X2's frame_begin: L / R turn the pause page; the X1 page holds back the
 * cursor and choice buttons. */
void MmxX2PauseFrameBegin(uint32_t *inputs);
/* Zero's hidden X2 weapons (owned bits taken away while his menu is up). */
uint16_t MmxX2PauseZeroHidden(void);
void MmxX2PauseSetZeroHidden(uint16_t hidden);
/* After X2's PPU frame: the page's tag, and the X1 page's blank slots. */
void MmxX2PauseAfterDraw(void);
/* Snapshot bytes (X2's bridge chunk). */
uint16_t MmxX2PauseState(void);
void MmxX2PauseSetState(uint16_t state);

#ifdef __cplusplus
}
#endif

#endif /* MMX_X2PAUSE_H_ */
