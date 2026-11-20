#include "mmx_escape.h"
#include "mmx_mod_constructor.h"

/* Escape Bugs (megaman-x.accessibility.escape): ESCAPE.U's commands. */
static void activate(void) {
  MmxEscapeSetEnabled(true);
  MmxEscapeRegisterHooks();
}
static void reset(void) { MmxEscapeSetEnabled(false); }
SNES_MOD_CONSTRUCTOR(mmx_register_escape_plugin) {
  snes_mod_register_reset_callback(reset);
  snes_mod_register_activation_plugin("megaman-x.escape", activate);
}
