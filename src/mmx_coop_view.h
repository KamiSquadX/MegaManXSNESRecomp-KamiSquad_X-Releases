#pragma once
#include "mmx_coop.h"

/* The active independent-world policy is shared by local split and online
 * independent cameras. Selecting a presentation seat never changes it. */
typedef struct MmxCoopView { int x, y; unsigned player; } MmxCoopView;
void MmxCoopViewsSetActive(bool active);
bool MmxCoopViewsActive(void);
/* Older name, kept for generated-code hooks: independent views are active. */
bool MmxCoopViewsOnline(void);
void MmxCoopViewsSetLocalSplit(bool enabled);
bool MmxCoopViewsLocalSplit(void);
void MmxCoopViewsSetIndependent(bool independent);
bool MmxCoopViewsIndependent(void);

/* Per-seat copies of the native $81:F6DC camera-region commands. These
 * affect culling and pit checks, so they belong to the rollback state. */
typedef struct MmxCoopViewBoundsState {
  uint16_t limits[2][4]; /* left, right, top, bottom */
  uint8_t valid, stage;
  /* Native current limits ease toward the authored targets above. Camera
   * history advances once per simulation frame, never during presentation. */
  uint16_t current[2][4], camera[2][2];
  uint8_t camera_valid;
} MmxCoopViewBoundsState;
void MmxCoopViewsSetRom(const uint8_t *rom, size_t size);
MmxCoopViewBoundsState MmxCoopViewsGetBoundsState(void);
bool MmxCoopViewsValidBoundsState(const MmxCoopViewBoundsState *bounds);
void MmxCoopViewsSetBoundsState(const MmxCoopViewBoundsState *bounds);
void MmxCoopViewsUpdateBounds(const uint8_t *ram, const MmxCoopState *state);
void MmxCoopViewsEndFrame(const uint8_t *ram, const MmxCoopState *state);
/* Before a remote player takes a native door, transfer its authored view to
 * the shared script camera while both independent views still exist. */
/* Like MmxCoopViewsAdoptPlayer, for a seat that is dying or beaming: the
 * native camera and limits move to that seat's own view. */
void MmxCoopViewsAdoptSeatView(uint8_t *r,const MmxCoopState *s,unsigned seat);
/* A seat placed beside another takes that seat's camera limits at once. */
/* Checkpoint 0 of `stage` ($80:E68E record, bank $86): the player's x, the
 * beam's starting y and the room limits, as entering from stage select. */
bool MmxCoopViewsStageStart(unsigned stage,int *x,int *y,int limits[4]);
/* Give `seat` these room limits; the world actor's are written natively. */
void MmxCoopViewsSetSeatLimits(uint8_t *r,const MmxCoopState *s,unsigned seat,const int limits[4]);
void MmxCoopViewsCopyBounds(const uint8_t *r,const MmxCoopState *s,unsigned to,unsigned from);
bool MmxCoopViewsAdoptPlayer(uint8_t *ram, const MmxCoopState *state, unsigned seat);
void MmxCoopViewsPlayerLimits(const uint8_t *ram, const MmxCoopState *state,
                            unsigned seat, int limits[4]);
MmxCoopView MmxCoopViewWithBounds(const uint8_t *ram, const MmxCoopState *state,
    unsigned seat, const MmxCoopViewBoundsState *bounds);
typedef struct MmxCoopViewWorldState {
  uint8_t seen[256],stage,initialized;
  /* Balanced native world/contact continuations must survive rollback. */
  uint8_t actor_return,contact_player;
} MmxCoopViewWorldState;
MmxCoopViewWorldState MmxCoopViewsGetWorldState(void);
void MmxCoopViewsSetWorldState(const MmxCoopViewWorldState *state);
void MmxCoopViewsResetWorld(void);
bool MmxCoopViewsSeen(unsigned flag);
void MmxCoopViewsMark(unsigned flag,bool seen);
void MmxCoopViewsBeginStage(unsigned stage);
void MmxCoopViewsActorReturn(unsigned player_plus_one);
void MmxCoopViewsContactPlayer(unsigned player);
MmxCoopView MmxCoopViewForPlayer(const uint8_t *ram, const MmxCoopState *state, unsigned seat);
bool MmxCoopViewContains(const uint8_t *ram, const MmxCoopState *state,
                         int x, int y, int left, int right, int top, int bottom, int margin);
/* The same test for one seat's view only (false for a seat not in play). */
bool MmxCoopViewSeatContains(const uint8_t *ram, const MmxCoopState *state, unsigned seat,
                             int x, int y, int left, int right, int top, int bottom, int margin);
