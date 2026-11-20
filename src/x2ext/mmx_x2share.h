#ifndef MMX_X2SHARE_H_
#define MMX_X2SHARE_H_
/* Heart Tanks, Sub Tanks and extra lives shared by the X1 and X2 pages
 * (mmx_x2share.c). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MmxX2ShareReset(void);
/* A page turn: the page being left (its WRAM, before it is put aside)... */
void MmxX2ShareLeave(const uint8_t *ram, bool x2);
/* ...and the page coming in (its WRAM, just restored). */
void MmxX2ShareEnter(uint8_t *ram, bool x2);
/* Each frame of the running page, inside its simulation. */
void MmxX2ShareFrame(uint8_t *ram, bool x2);
/* Boss weapons as a game last had them (recorded at each page turn, and when
 * X2's page is prepared): X2's beaten bosses by X2 weapon (1..8, X2's menu
 * order), and X1's weapon bytes $1F87..$1F98. */
void MmxX2ShareRecordWeapons(const uint8_t *ram, bool x2);
bool MmxX2ShareX2BossBeaten(unsigned weapon);
bool MmxX2ShareX1Weapons(uint8_t out[18]);
/* X1's armor parts ($1F99 bits 0-3) as last recorded (0 when none). */
uint8_t MmxX2ShareX1Armor(void);
void MmxX2ShareSetX1Armor(uint8_t armor);
/* Xtreme Sub Tanks (tanks five to eight upgrade slots 1-4): drawn over each
 * game's pause screen. X2: the finished frame from its top row (stride in
 * pixels) and the PPU's brightness table. X1: its composed frame, the 256
 * field starting at column `left`, while MmxX2ShareX1PauseShown. Their
 * energy goes with X2 snapshots (bridge chunk). */
void MmxX2ShareDrawX2Pause(uint32_t *top_row, int stride, const uint8_t *brightness);
bool MmxX2ShareX1PauseShown(const uint8_t *ram);
void MmxX2ShareDrawX1Pause(uint32_t *pixels, int width, int left);
/* Blue health (Heart Tanks past eight), over each game's HP bar in play. */
bool MmxX2ShareBlueShown(const uint8_t *ram, bool x2);
void MmxX2ShareDrawX1Blue(uint32_t *pixels, int stride, int left);
void MmxX2ShareDrawX2Blue(uint32_t *top_row, int stride);
void MmxX2ShareXtremeSave(uint8_t out[4]);
void MmxX2ShareXtremeLoad(const uint8_t in[4]);
/* Sub Tanks five to eight can be picked up (interpreter twins of the
 * generated-code hooks; once, at activation). */
void MmxX2ShareRegisterHooks(void);
/* Everything above as bytes (the in-game launcher's restart keeps it). */
size_t MmxX2ShareBlob(void *out, size_t cap);
bool MmxX2ShareSetBlob(const void *in, size_t size);
/* The same, for a snapshot load (no resync). */
bool MmxX2ShareLoadState(const void *in, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MMX_X2SHARE_H_ */
