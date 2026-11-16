#ifndef MMX_X2EXT_H_
#define MMX_X2EXT_H_
/* X-tensions: X2 game extension (alpha).
 *
 * Mega Man X2 runs inside this executable as a second program: its own
 * generated code (module "x2", every symbol prefixed x2_), its own machine
 * and its own frame driver. On the stage select, R turns the page to X2's
 * stage select and L turns back. Each program keeps its own machine (and so
 * its own cart, with whatever its plugins patched in) and a snapshot of where
 * it was left; the host swaps them at a frame boundary. */
#include <stdbool.h>
#include "desktop/host_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The two programs' host descriptors and X1's frame driver, from main.c. */
void MmxX2ExtSetHost(const SnesDesktopHostGame *x1, const SnesDesktopHostGame *x2);
/* Per frame, from each program's after_run_frame. */
void MmxX2ExtAfterFrame(const SnesDesktopHostFrameStats *stats);
/* X2 is the running program. */
bool MmxX2ExtInX2(void);
/* While X2 runs: X2 is on a menu screen (outside its stages, $D0 = 2 with
 * $D1 = 4), where KEY CONFIG's MENU CONFIRM is the game's Y. */
bool MmxX2ExtInMenu(void);
/* A program swap is restoring a program's machine. */
bool MmxX2ExtSwapping(void);
/* X1's frame (mmx_rtl.c), inside its simulation: progress shared with X2. */
#include <stdint.h>
void MmxX2ExtX1Frame(uint8_t *ram);
/* X1's frame_begin (mmx_options.c): X2 Password Saves may press Start for
 * X1's password page as it comes back. */
void MmxX2ExtX1Inputs(uint32_t *inputs);
/* The in-game launcher's restart (SnesDesktopHostGame.save_resume_state /
 * on_resume_state): X1's moment in `path`, X2's page beside it, and X2
 * running again after the restart if it was. */
int MmxX2ExtSaveResume(const char *path);
/* Replays (mmx_replay.c): the running program, -1 without the extension,
 * 0 X1, 1 X2, 2 X2's password page; and a page turn toward `program` at the
 * next frame boundary (one step: X2 <-> X1 <-> X2's password page). */
int MmxX2ExtProgramId(void);
void MmxX2ExtRequestProgram(int program);
/* X2's RtlGameInfo.frame_end. */
void MmxX2ExtFrameEnd(void);
void MmxX2ExtResumeLoaded(const char *path, int loaded);

#ifdef __cplusplus
}
#endif

#endif /* MMX_X2EXT_H_ */
