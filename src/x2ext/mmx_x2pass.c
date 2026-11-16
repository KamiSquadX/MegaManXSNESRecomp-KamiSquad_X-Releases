/* X2 Password Saves (SRAM) (X-tensions: under the X2 game extension).
 *
 * X2's counterpart of Password Saves (mods/mmx_password_plugin.c): the last
 * password X2 showed is kept in a small host file and filled in when X2's
 * password screen opens. X2's passwords are sixteen digits (X1's twelve):
 *
 *   $00:EE66..EE6D  the entry screen (D = $1E58) copies its default digits
 *                   from $7E:FFCB into $1E74..$1E83; at $00:EE6F they are
 *                   replaced by the saved ones.
 *   $00:EFE1        the generated-password screen waits for a button; its
 *                   digits are at $7E:FFCB..FFDA (written at $00:F15B).
 *
 * Both run interpreted; the hooks are X2's (owner 1). The page turn between
 * the two password screens lives in mmx_x2ext.c. Offline only, like X1's
 * Password Saves: local progress is not a match input. */
#include "mmx_x2pass.h"

#include <stdio.h>
#include <string.h>

#include "host_paths.h"
#include "mmx_netplay.h"
#include "mods/mmx_mod_constructor.h"
#include "recomp_launcher.h"
#include "snes/interp_bridge.h"
#include "cpu_state.h"
#include <stdlib.h>

#define X2PASS_PACKAGE "megaman-x.xtension.x2-password-save"
#define X2PASS_HEADER "MMX2-PASSWORD 1\n"

extern uint8_t g_ram[0x20000];

static bool s_active;
static char s_path[4096];
static char s_digits[MMX_X2PASS_DIGITS + 1];

bool MmxX2PassEnabled(void) { return s_active; }

static bool valid_digits(const char *d) {
  if (strlen(d) != MMX_X2PASS_DIGITS) return false;
  for (unsigned i = 0; i < MMX_X2PASS_DIGITS; ++i)
    if (d[i] < '1' || d[i] > '8') return false;
  return true;
}

static bool write_record(const char *digits) {
  char temporary[4200];
  snprintf(temporary, sizeof(temporary), "%s.tmp", s_path);
  FILE *f = fopen(temporary, "wb");
  if (!f) return false;
  const bool ok = fprintf(f, "%s%s\n", X2PASS_HEADER, digits) > 0;
  if (fclose(f) != 0 || !ok) return false;
  remove(s_path);
  return rename(temporary, s_path) == 0;
}

static bool open_file(const char *path) {
  s_digits[0] = 0;
  snprintf(s_path, sizeof(s_path), "%s", path);
  FILE *f = fopen(s_path, "rb");
  if (!f) {
    if (!write_record("")) {
      fprintf(stderr, "[x2-password] Cannot create save file: %s\n", s_path);
      return false;
    }
    fprintf(stderr, "[x2-password] Using %s (empty)\n", s_path);
    return true;
  }
  char data[96] = {0};
  const size_t n = fread(data, 1, sizeof(data) - 1, f);
  fclose(f);
  const size_t header = strlen(X2PASS_HEADER);
  if (n <= header || memcmp(data, X2PASS_HEADER, header) || data[n - 1] != '\n') {
    fprintf(stderr, "[x2-password] Unrecognized password save; left unchanged: %s\n", s_path);
    return false;
  }
  data[n - 1] = 0;
  const char *digits = data + header;
  if (*digits && !valid_digits(digits)) {
    fprintf(stderr, "[x2-password] Invalid password save; left unchanged: %s\n", s_path);
    return false;
  }
  snprintf(s_digits, sizeof(s_digits), "%s", digits);
  fprintf(stderr, "[x2-password] Using %s (%s)\n", s_path, s_digits[0] ? "password saved" : "empty");
  return true;
}

/* X2's password entry screen just copied its defaults ($1E74..$1E83). */
static void prefill(void) {
  if (!s_active || !s_digits[0]) return;
  for (unsigned i = 0; i < MMX_X2PASS_DIGITS; ++i) g_ram[0x1e74 + i] = (uint8_t)(s_digits[i] - '1');
  fprintf(stderr, "[x2-password] Prefilled %s\n", s_digits);
}

/* X2's generated password is on screen ($7E:FFCB..FFDA). */
static void capture(void) {
  if (!s_active) return;
  char digits[MMX_X2PASS_DIGITS + 1];
  for (unsigned i = 0; i < MMX_X2PASS_DIGITS; ++i) {
    const uint8_t v = g_ram[0xffcb + i];  /* $7E:FFCB */
    if (v > 7) return;
    digits[i] = (char)('1' + v);
  }
  digits[MMX_X2PASS_DIGITS] = 0;
  if (!strcmp(digits, s_digits)) return;
  if (!write_record(digits)) {
    fprintf(stderr, "[x2-password] Cannot update save; previous file preserved: %s\n", s_path);
    s_active = false;
    return;
  }
  snprintf(s_digits, sizeof(s_digits), "%s", digits);
  fprintf(stderr, "[x2-password] Saved %s\n", s_digits);
}

bool MmxX2PassHasSaved(void) { return s_active && s_digits[0]; }

/* X2 shows a password only after a game over, so the progress is saved from
 * the stage select too: X2's generator ($00:F013, D = $1E58, tables in bank
 * 6) writes the digits to $7E:FFCB..FFDA; it runs on a copy of the CPU and
 * everything it touches in WRAM is put back. */
void MmxX2PassSaveProgress(void) {
  if (!s_active) return;
  static uint8_t saved[0x20000];
  memcpy(saved, g_ram, sizeof(saved));
  CpuState c = g_cpu;
  c.D = 0x1e58;
  c.DB = 6;
  c.PB = 0;
  c.P |= 0x30;
  c.m_flag = c.x_flag = 1;
  const int ok = interp_bridge_run(&c, 0x00f013);
  if (ok) capture();
  memcpy(g_ram, saved, sizeof(saved));
}

static void hook(CpuState *cpu, uint32_t pc) {
  if (cpu->D != 0x1e58) return;
  switch (pc & 0x7fffff) {
    case 0x00ee6f: prefill(); break;
    case 0x00efe1: capture(); break;
  }
}

static void reset(void) { s_active = false; }

static void activate(void) {
  if (MmxNetplayActive()) return;
  char path[4096];
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource = {0};
  if (provider && provider->feature_resource_get &&
      provider->feature_resource_get(provider->ctx, X2PASS_PACKAGE, "password_save", 0, &resource) &&
      resource.path[0]) {
    if (strlen(resource.path) >= sizeof(path)) return;
    snprintf(path, sizeof(path), "%s", resource.path);
  } else if (!snesrecomp_exe_dir_path("mmx2-password.srm", path, sizeof(path))) {
    fprintf(stderr, "[x2-password] Cannot locate executable directory\n");
    return;
  }
  if (!open_file(path)) return;
  s_active = true;
  static bool hooked;
  if (!hooked) {
    hooked = true;
    interp_bridge_set_pre_opcode_hook_owner(1);
    (void)interp_bridge_add_pre_opcode_hook(0x00ee6f, hook);
    (void)interp_bridge_add_pre_opcode_hook(0x00efe1, hook);
    interp_bridge_set_pre_opcode_hook_owner(0);
  }
}

SNES_MOD_CONSTRUCTOR(mmx_register_x2pass_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.xtension.x2-password-save", activate);
}
