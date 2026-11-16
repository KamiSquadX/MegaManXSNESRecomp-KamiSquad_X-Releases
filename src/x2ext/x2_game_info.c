/* X2's frame driver as the runtime sees it (from MegaManX2Recomp's
 * src/x2_cpu_infra.c). Replays record X2's frames too (MmxX2ExtFrameBegin /
 * MmxX2ExtFrameEnd), each checkpoint tagged with its program. */
#include "x2_runtime.h"

/* Not const: Tier 2 diagnostics sets tier2_capture when X2 starts. */
RtlGameInfo kX2ExtGameInfo = {
  .title = "x2",
  .frame_begin = &MmxX2ExtFrameBegin,
  .frame_end = &MmxX2ExtFrameEnd,
  .run_frame = &X2RunOneFrameOfGame,
  .draw_ppu_frame = &X2DrawPpuFrame,
  .save_name_prefix = "x2save",
  .state_save_extra = &X2StateSaveExtra,
  .state_load_extra = &X2StateLoadExtra,
  .on_state_loaded = &X2OnStateLoaded,
};

/* The generated module_v2.c registers itself from a static constructor, which
 * MSVC's release linker discards because nothing references it. Register the
 * same tables explicitly (module_namespace.h maps these names to x2_*). */
#include <string.h>
#include "program_module.h"
#include "module_namespace.h"
extern const DispatchEntry g_dispatch_table[];
extern const unsigned g_dispatch_table_count;
extern const RamRoutineGuard g_ram_routine_guards[];
extern const unsigned g_ram_routine_guard_count;

const SnesProgramModule *MmxX2ExtModule(void) {
  static SnesProgramModule module;
  const SnesProgramModule *found = snes_program_module_find("x2");
  if (found) return found;
  if (!module.id) {
    static const uint8_t digest[32] = {
      0xf3, 0x24, 0x67, 0x55, 0xf6, 0x08, 0xa1, 0xe1,
      0xdc, 0x9c, 0x84, 0x8b, 0x61, 0xda, 0x3b, 0x82,
      0x4c, 0x78, 0x53, 0xb2, 0x9b, 0x3b, 0xe4, 0x0d,
      0xf6, 0xfc, 0x7f, 0x27, 0x93, 0xa8, 0x87, 0xed,
    };
    module.id = "x2";
    module.symbol_prefix = "x2";
    module.dispatch = g_dispatch_table;
    module.dispatch_count = g_dispatch_table_count;
    module.guards = g_ram_routine_guards;
    module.guard_count = g_ram_routine_guard_count;
    module.rom_size = 1572864u;
    memcpy(module.rom_sha256, digest, sizeof(digest));
    (void)snes_program_module_register(&module);
  }
  return &module;
}
