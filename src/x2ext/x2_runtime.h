#ifndef MMX_X2EXT_X2_RUNTIME_H_
#define MMX_X2EXT_X2_RUNTIME_H_
/* Mega Man X2's frame driver and save-state cursor, linked into the Mega Man X
 * executable as the "x2" program module (X-tensions: X2 game extension). */
#include <stdbool.h>
#include <stdint.h>
#include "common_rtl.h"
#include "common_cpu_infra.h"

struct SaveLoadInfo;
void X2RunOneFrameOfGame(void);
void X2DrawPpuFrame(void);
bool X2UseNativeFrame(void);
void X2StateSaveExtra(struct SaveLoadInfo *sli);
void X2StateLoadExtra(struct SaveLoadInfo *sli, uint32_t version);
void X2OnStateLoaded(uint32_t version);

/* X2's RtlGameInfo: its own frame driver, presentation and save-slot names
 * (x2save<N>.sav), so X1 and X2 save states never share a file. */
extern RtlGameInfo kX2ExtGameInfo;
/* Its frame_begin (mmx_x2ext.c): scripted input while X2 first boots. */
bool MmxX2ExtFrameBegin(uint32_t *inputs);
void MmxX2ExtFrameEnd(void);

/* X2's generated program (module "x2"), registered on first use. */
struct SnesProgramModule;
const struct SnesProgramModule *MmxX2ExtModule(void);

#endif /* MMX_X2EXT_X2_RUNTIME_H_ */
