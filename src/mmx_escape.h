#ifndef MMX_ESCAPE_H_
#define MMX_ESCAPE_H_
/* Escape Bugs (Accessibility): commands on the pause menu's ESCAPE.U (X1
 * item 9, X2's EXIT, cursor $1A) while it is highlighted, for getting
 * unstuck (mmx_escape.c):
 *   X             the camera back on the player (and inside its room)
 *   Select        to standing ground on screen: left, right, middle in turn
 *   hold B 3 s    back to the last checkpoint, no life lost
 *   hold Start 3 s  stage select, whether the stage is beaten or not
 * A short press of B or Start does what it always did there. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MmxEscapeSetEnabled(bool enabled);
bool MmxEscapeEnabled(void);
/* Each frame's inputs (seat 0 bits 0-11, seat 1 bits 12-23), after replay
 * playback and before the frame runs: X1's page or X2's. */
void MmxEscapeFrameBegin(uint32_t *inputs, bool x2);
/* The pause menu's escape check (X1 $00:C93C, X2 $00:C622) answers
 * "available" for a held Start. */
bool MmxEscapeForceExit(void);
/* The command list, over each game's pause screen. X1: its composed frame,
 * the 256 field from column `left`. X2: its finished frame from the top
 * row, with the PPU's brightness table. */
bool MmxEscapeX1ListShown(const uint8_t *ram);
void MmxEscapeDrawX1(uint32_t *pixels, int stride, int left);
void MmxEscapeDrawX2(uint32_t *top_row, int stride, const uint8_t *brightness);
/* Interpreter twins of the generated-code escape check hooks (once). */
void MmxEscapeRegisterHooks(void);

#ifdef __cplusplus
}
#endif

#endif /* MMX_ESCAPE_H_ */
