#ifndef MMX_X2COOP_H_
#define MMX_X2COOP_H_
/* Local co-op on the X2 page (mmx_x2coop.c). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mmx_zero.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CpuState CpuState;

/* Co-op is configured (the X1 co-op mod) and X2 runs it. */
bool MmxX2CoopActive(void);
void MmxX2CoopEnter(void);
void MmxX2CoopLeave(void);
/* Once per X2 frame, before the game runs: pads, joining, respawns. */
void MmxX2CoopFrameBegin(void);
/* $08:94C3, just before the player routine's PLD/PLP/RTL. Nonzero: the
 * routine's prologue push was undone and player 2's pass starts at $08:9428. */
int MmxX2CoopPassEnd(CpuState *cpu);
void MmxX2CoopRegisterHooks(void);
/* $08:C325, the PLD of the player's terrain update $08:C314: nonzero runs it
 * again (from $08:C314) for player 2. */
int MmxX2CoopTerrainEnd(CpuState *cpu);
/* Enemy contact $08:D752: its entry, and each RTL (nonzero: restart it). */
void MmxX2CoopContactBegin(CpuState *cpu);
int MmxX2CoopContactEnd(CpuState *cpu);
/* Inside a player pass ($08:9479 / $08:947E): the live seat's Zero state. */
void MmxX2CoopPassBegin(void);
/* The seat whose body is live at $09D8 owns Zero's per-seat state. */
bool MmxX2CoopLiveOwnsZero(void);
/* A seat drawn by the bridge (Zero's body, not X2's own sprites): its
 * stored body and Zero state, or NULL when it is not on screen. */
const uint8_t *MmxX2CoopDrawnSeat(MmxZeroState *state, unsigned *seat);
/* The live seat is X (X2 draws it): its sprites stay. */
bool MmxX2CoopLiveIsX(void);
/* The seat whose Zero state runs now (its controller's extended buttons
 * drive KSX: Giga Attack and the rest), or -1 off X2 co-op. */
int MmxX2CoopZeroSeat(void);
/* Player 2's Start pauses, and drives the menu it opened. */
void MmxX2CoopMenuInputs(uint32_t *inputs);
/* Objects testing the player ($08:D865) with player 2 too (generated code
 * hooks: run it interpreted; the object loops' returns end a kept seat). */
int MmxX2CoopOverlapInterp(void);
void MmxX2CoopObjectDone(void);
int MmxX2CoopMainBelowOk(void);
/* The other seat's stored body while it plays, else NULL. */
uint8_t *MmxX2CoopOtherBody(void);
/* Player 2's meter, over the finished frame (Horizontal / Compact). */
void MmxX2CoopDrawHud(int badge_x, int badge_y);

/* Side by side (Local co-op display): co-op runs with the setting chosen. */
bool MmxX2CoopSplit(void);
/* Two cameras: Side by side, or Netplay cameras: Independent. */
bool MmxX2CoopIndependent(void);
/* Extra object window around player 1's camera (X2's widescreen spawn
 * margin), so objects in player 2's view stay alive; 0 when not split. */
int MmxX2CoopSplitMargin(void);
/* Both cameras for this drawn frame (player 2's follows player 2). False
 * outside a stage: both panes show the shared frame. */
bool MmxX2CoopSplitCameras(int *c1x, int *c1y, int *c2x, int *c2y);
/* Player 2's meter in player 2's pane (g_ppu is that pane). */
void MmxX2CoopDrawPaneHud(int badge_x, int badge_y);
/* Co-op physics diagnostics (developer mod): this frame's rows, at the end
 * of X2's frame. */
void MmxX2CoopDiagnosticFrame(void);

/* Snapshot bytes for the seats (mmx_x2zero.c's bridge state carries them). */
size_t MmxX2CoopStateSize(void);
void MmxX2CoopSaveState(uint8_t *out);
bool MmxX2CoopLoadState(const uint8_t *in, size_t size);

/* mmx_x2split.c: player 2's pane. */
bool MmxX2SplitWanted(void);
/* Around X2DrawPpuFrame: keep the field's starting PPU state, then draw
 * player 2's pane from it. */
void MmxX2SplitFrameStart(void);
void MmxX2SplitRenderPane(void);
/* Host presenter: both panes side by side (frame_w = 2 * 256). */
void MmxX2SplitPrepareFrame(int *frame_w, int *frame_h);
int MmxX2SplitDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* MMX_X2COOP_H_ */
