#ifndef SMW_SMW_RTL_H_
#define SMW_SMW_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

void MmxDrawPpuFrame(void);
void RunOneFrameOfGame(void);
void MmxSchedulerTick(void);
/* Native graphics-task checkpoint, shared by interpreted and compiled calls. */
bool MmxGraphicsShouldYield(const CpuState *cpu);
/* Match the generated widescreen comparisons when co-op enters the interpreter. */
void MmxWsCullHook(CpuState *cpu, uint32_t pc);
/* The player's screen-edge wall ($00:E0F9 / $00:E115 in the camera routine),
 * widened to the visible widescreen edges; that margin (0 outside widescreen
 * stages); and the interpreter-routing policy for $00:DE40. */
void MmxWsPlayerEdgeHook(CpuState *cpu, uint32_t pc);
int MmxWsPlayerEdgeMargin(void);
bool MmxCameraRouteDE40(const CpuState *cpu);

/* .sav v5 game chunk + post-load fiber rebuild (RtlGameInfo hooks). */
struct SaveLoadInfo;
void MmxStateSaveExtra(struct SaveLoadInfo *sli);
void MmxStateLoadExtra(struct SaveLoadInfo *sli, uint32_t version);
void MmxOnStateLoaded(uint32_t version);
/* RtlGameInfo.hardware_reset: reboot through I_RESET after the Reset hotkey. */
void MmxOnHardwareReset(void);

#endif  // SMW_SMW_RTL_H_
