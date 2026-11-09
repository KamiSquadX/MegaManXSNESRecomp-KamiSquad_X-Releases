#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../mmx_zero.h"

/* The Saber-owned per-frame bridge registered at Zero's pre-player seam. */
const MmxZeroExtension *MmxSaberFrameExtension(void);
void MmxSaberFrameReset(void);

/* Whether X1's handler for player action $0BAA can fire the buster. The
 * Saber swings only where it can (hurt is handled separately). */
bool MmxSaberFrameNativeFireAction(uint8_t action);

/* The physical X/Y buttons of the seat being run. Single player reads the
 * native joypad words ($A7 held, $A9 previous, $AC newly pressed); co-op
 * supplies each seat's own pad, and Saber then tracks X's previous state. */
typedef struct MmxSaberFramePad {
  bool x_held, y_pressed;
  uint16_t pad;   /* the seat's whole SNES joypad word (KSX Zero) */
} MmxSaberFramePad;
typedef void (*MmxSaberFramePadSource)(const uint8_t *ram, MmxSaberFramePad *pad);
void MmxSaberFrameSetPadSource(MmxSaberFramePadSource source);
/* The running seat's mapped extended actions (RtlGetPadExt). KSX Zero's
 * buster is ARMOR ABILITY (R2) and its Giga Attack GIGA ATTACK (L2). */
void MmxSaberFrameSetExtSource(uint8_t (*source)(void));

/* Live RAM retained by the Saber-owned player seams for presentation reads. */
const uint8_t *MmxSaberFrameRam(void);

/* Bytes of the Saber savestate blob (must fit MMX_ZERO_EXTENSION_STATE_BYTES). */
size_t MmxSaberFrameStateBytes(void);

/* Test-only observability for the deliberate X pass-through path. */
bool MmxSaberFrameLastWroteInput(void);
