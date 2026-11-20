/* Zero on the X2 page (X-tensions: X2 game extension), first step.
 *
 * Add Zero's code addresses X1's RAM. X2's player struct has X1's exact field
 * layout at another base ($09D8 vs $0BA8), uses X1's action and animation
 * numbers, and moves a handful of globals (docs/x2-zero-port.md). So Zero's
 * code runs here against an X1-layout view of X2's RAM: the view is X2's RAM
 * with the player struct, its helper objects and the mapped globals placed at
 * their X1 addresses, and the player struct is written back afterwards.
 *
 * This step covers the character exchange (Select, Zero's own teleport timing
 * and health pools) and drawing Zero's X3 body in place of X's sprites. In
 * X2 Zero still moves and fires with X's native moves; his own buster, saber
 * and KSX skills need X2 hooks in the player code and come later. */
#include "mmx_x2zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmx_hitbox_overlay.h"
#include "mmx_x2coop.h"
#include "mmx_x2ext.h"
#include "mmx_x2share.h"
#include "mmx_x2pause.h"
int X2WsHudLeftShift(void); /* x2_runtime.c: widescreen HUD shift */
#include "mmx_renderer.h"
#include "mmx_weapon_combat.h"
#include "mmx_zero.h"
#include "mmx_options.h"
#include "saber/mmx_saber_tuning.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/cart.h"
#include "snes/snes.h"

extern uint8_t g_ram[0x20000];
extern Ppu *g_ppu;
extern Snes *g_snes;

/* X2's player struct is X1's at $09D8 (and $80 bytes longer). Everything
 * from X1's $0C38 to $1428 -- player helpers, 32-byte effects, enemies
 * ($0E68) and player shots ($1228) -- sits $150 lower in X2 with the same
 * object layout; X2 then has a ninth shot slot ($12D8) before its enemy
 * shots ($1318, X1 $1428). */
enum {
  X1_PLAYER = 0x0ba8, X2_PLAYER = 0x09d8, PLAYER_SIZE = 0x90,
  /* The sound command ring ($80:88CD, X2 $80:8552) and its index, just
   * below the player: X1 $0B72..$0BA7, X2 $09A2..$09D7. */
  X1_SOUND = 0x0b72, X2_SOUND = 0x09a2, SOUND_SIZE = 0x36,
  X1_OBJECTS = 0x0c38, X2_OBJECTS = 0x0ae8, OBJECTS_SIZE = 0x1428 - 0x0c38,
  X1_ENEMY_SHOTS = 0x1428, X2_ENEMY_SHOTS = 0x1318, ENEMY_SHOTS_SIZE = 0x200,
};
/* X1 global -> X2 global. 0 = X2 has no such state; the view reads 0. */
static const struct { uint16_t x1, x2; } kGlobals[] = {
  {0x1f99, 0x1fd0}, {0x1f0c, 0x1f1d}, {0x1f0d, 0x1f1e}, {0x1f23, 0x1f36},
  {0x1f31, 0x1f45}, {0x1f7a, 0x1fbf}, {0x1f86, 0x1fb9},
  {0x1f13, 0x1f25}, {0x1f14, 0x1f26}, {0x1f15, 0x1f27}, {0x1f16, 0x1f28},
  {0x1f17, 0x1f29}, {0x1f18, 0x1f2a}, {0x1f19, 0x1f2b},
  {0x1f10, 0}, {0x1f48, 0},
};
/* Cameras: X1 $1E4D/$1E50 (X, Y), X2 $1E5D/$1E60. */
static const struct { uint16_t x1, x2; } kWords[] = {
  {0x1e4d, 0x1e5d}, {0x1e50, 0x1e60},
};

static uint8_t view[0x20000];
static MmxZeroState x1_state, x2_state;
static uint8_t *x1_rom;
static size_t x1_rom_size;
/* Add Axl's body hooks belong to X1; Axl is not ported to X2. */
static const MmxZeroBodyHooks *x1_body_hooks;
static bool x2_state_valid, select_edge;
static MmxZeroExtensionGate x1_gate;
static uint16_t previous_pad;

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

/* Co-op: the player the view shows, when it is a seat stored away from
 * $09D8 (mmx_x2coop.c keeps Zero's seat here between its passes). */
static uint8_t *player_src;
void MmxX2ZeroSetPlayerSource(uint8_t *body) { player_src = body; }
static uint8_t *player_body(void) { return player_src ? player_src : g_ram + X2_PLAYER; }

static bool x2_gameplay(void) {
  return g_ram[0xd0] == 2 && g_ram[0xd1] == 4;
}

static unsigned to_x1(unsigned a);

static uint8_t *build_view(void) {
  memcpy(view, g_ram, sizeof(view));
  memcpy(view + X1_PLAYER, player_body(), PLAYER_SIZE);
  memcpy(view + X1_SOUND, g_ram + X2_SOUND, SOUND_SIZE);
  memcpy(view + X1_OBJECTS, g_ram + X2_OBJECTS, OBJECTS_SIZE);
  memcpy(view + X1_ENEMY_SHOTS, g_ram + X2_ENEMY_SHOTS, ENEMY_SHOTS_SIZE);
  for (unsigned i = 0; i < sizeof(kGlobals) / sizeof(kGlobals[0]); ++i)
    view[kGlobals[i].x1] = kGlobals[i].x2 ? g_ram[kGlobals[i].x2] : 0;
  for (unsigned i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
    view[kWords[i].x1] = g_ram[kWords[i].x2];
    view[kWords[i].x1 + 1] = g_ram[kWords[i].x2 + 1];
  }
  /* X1's boss weapons as X1 last had them ($1F87..$1F98): KSX Zero learns a
   * Special Move from its X1 Maverick ($1F88 + 2n bit 6), here as in X1, and
   * from nothing else (no X1 record: none; never X2's RAM there). */
  if (!MmxX2ShareX1Weapons(view + 0x1f87)) memset(view + 0x1f87, 0, 18);
  /* His armor parts ($1F99 bits 0-3: the Saber's purple arms and the rest)
   * are X1's too, not X2's capsules ($1FD0 bits 0-3, X's own); the high bits
   * stay X2's tank flags. */
  view[0x1f99] = (uint8_t)((g_ram[0x1fd0] & 0xf0) |
      (MmxSaberTuningStartAllUpgrades() ? 0x0f : MmxX2ShareX1Armor()));
  /* Game mode: X2's $D0..$D3 are X1's $D1..$D4 (X1 gameplay is $D1 = 2,
   * $D2 = 4, $D3 = 4; X2's is $D0 = 2, $D1 = 4, $D2 = 4). */
  view[0xd1] = g_ram[0xd0];
  view[0xd2] = g_ram[0xd1];
  view[0xd3] = g_ram[0xd2];
  view[0xd4] = g_ram[0xd3];
  /* Enemy contact ($84:9E15 / X2 $88:DA9B): the projectile the native code
   * is resolving ($1F1E, X2 $1F30, an object address) and its reaction
   * byte ($1F1D, X2 $1F2F). */
  {
    const unsigned p = to_x1(g_ram[0x1f30] | (unsigned)g_ram[0x1f31] << 8);
    view[0x1f1e] = (uint8_t)p; view[0x1f1f] = (uint8_t)(p >> 8);
    view[0x1f1d] = g_ram[0x1f2f];
  }
  /* Joypad copies from the NMI (X1 $80:81AF, X2 $88:85AD): held, previous
   * and new presses are one byte higher in X2 ($A8, $AA, $AC, $AE). */
  memcpy(view + 0xa7, g_ram + 0xa8, 8);
  return view;
}

/* Sound commands Zero's X1 code queues, in X2's numbering: X2's player code
 * plays the same sounds under other IDs. Teleport out X1 $0F ($81:8D05) is
 * X2 $11 ($88:B214); beam arrival $0E ($81:8A18) is $10 and its landing $0A
 * is $0F (both in the action $22 handler, X2 $88:A601/A691). X1's $0F in X2
 * is the death sound. */
static uint8_t x2_sound(uint8_t command) {
  switch (command) {
    case 0x0f: return 0x11;
    case 0x0e: return 0x10;
    case 0x0a: return 0x0f;
    default: return command;
  }
}

static void write_back(void) {
  memcpy(player_body(), view + X1_PLAYER, PLAYER_SIZE);
  /* Entries queued since the view was built (ring at +$00, index +$31). */
  for (unsigned i = g_ram[X2_SOUND + 0x31] & 0x1e; i != (view[X1_SOUND + 0x31] & 0x1eu);
       i = (i + 2) & 0x1e)
    view[X1_SOUND + i] = x2_sound(view[X1_SOUND + i]);
  memcpy(g_ram + X2_SOUND, view + X1_SOUND, SOUND_SIZE);
  memcpy(g_ram + X2_OBJECTS, view + X1_OBJECTS, OBJECTS_SIZE);
  memcpy(g_ram + X2_ENEMY_SHOTS, view + X1_ENEMY_SHOTS, ENEMY_SHOTS_SIZE);
  const uint8_t x2_armor = g_ram[0x1fd0] & 0x0f;
  for (unsigned i = 0; i < sizeof(kGlobals) / sizeof(kGlobals[0]); ++i)
    if (kGlobals[i].x2) g_ram[kGlobals[i].x2] = view[kGlobals[i].x1];
  /* X2's armor parts are X's: Zero's (X1's) never go back into them. */
  g_ram[0x1fd0] = (uint8_t)((g_ram[0x1fd0] & 0xf0) | x2_armor);
}

/* An X2 object address as X1's code knows it. */
static unsigned to_x1(unsigned a) {
  if (a >= X2_PLAYER && a < X2_PLAYER + PLAYER_SIZE) return a + (X1_PLAYER - X2_PLAYER);
  if (a >= X2_OBJECTS && a < X2_OBJECTS + OBJECTS_SIZE) return a + (X1_OBJECTS - X2_OBJECTS);
  if (a >= X2_ENEMY_SHOTS && a < X2_ENEMY_SHOTS + ENEMY_SHOTS_SIZE)
    return a + (X1_ENEMY_SHOTS - X2_ENEMY_SHOTS);
  return 0;
}

static bool debug_on(void) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("MMX_X2ZERO_DEBUG"); on = e && *e && *e != '0'; }
  return on != 0;
}

static bool zero_live(void) {
  return MmxZeroEnabled() && x2_gameplay();
}

/* ── Hooks in X2's generated player code (tools/apply_x2_zero_hooks.py) ── */

/* $88:9479, before the player's state dispatch (X1 $81:815C). */
void MmxX2ZeroPrePlayer(void) {
  MmxX2CoopPassBegin();
  if (!zero_live()) return;
  uint8_t *v = build_view();
  if (debug_on()) {
    static unsigned n;
    if (n++ % 30 == 0 || v[0xbdf] || v[0xbe3] || v[0xbde] || v[0xbe2])
      fprintf(stderr, "[x2zero] pre active=%d act=%02X/%02X seq=%02X held=%02X/%02X press=%02X/%02X d1=%02X d2=%02X d3=%02X\n",
              MmxZeroActive(), v[0xbaa], v[0xbab], v[0xbbf], v[0xbdf], v[0xbe1], v[0xbe2], v[0xbe3],
              v[0xd1], v[0xd2], v[0xd3]);
  }
  /* Entering gameplay (stage start, or respawn after a death): X2 has just
   * refilled the player, so both characters' pools refill too, as X1's
   * respawn hook ($80:9DA6) does. */
  static uint8_t last_mode;
  if (g_ram[0xd2] == 4 && last_mode != 4) MmxZeroHealthRespawn(v);
  last_mode = g_ram[0xd2];
  MmxZeroExtPrePlayer(v);
  MmxZeroSlideTick(v);
  MmxZeroMovementTick(v);
  MmxZeroPlayerTick(v);
  if (debug_on() && (v[0xbdf] || v[0xbe3] || MmxZeroGetState().slash))
    fprintf(stderr, "[x2zero]  after: held=%02X/%02X press=%02X/%02X slash=%u burst=%u act=%02X\n",
            v[0xbde], v[0xbdf], v[0xbe2], v[0xbe3], MmxZeroGetState().slash, MmxZeroGetState().burst, v[0xbaa]);
  write_back();
}

/* $88:947E, after it (X1 $81:8165). */
static void publish_all_bounds(void);

void MmxX2ZeroPlayerEnd(void) {
  MmxX2CoopPassBegin();
  if (!zero_live()) return;
  uint8_t *v = build_view();
  MmxZeroPlayerEnd(v);
  MmxZeroExtPlayerEnd(v);
  write_back();
  /* This frame's boxes, for collision and the hitbox overlay alike. */
  publish_all_bounds();
}

/* X2's player animation sequences (group $00 at $2F:A222) mapped to X1's
 * (group $18 at $AF:A210) by identical (timer, pose) records; Zero's pose
 * adapter is keyed on X1's numbers. $FF: an X2-only sequence (ride chaser,
 * X2's own moves), for which Zero's body follows X's pose number (+$17). */
static const uint8_t kX2ToX1Sequence[0x90] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0xff, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
  0x1f, 0x20, 0x21, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0xff, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
  0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4e, 0x4e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0x2a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/* $88:C690 / $88:C673: an object's animation starts / advances (X1
 * $84:8F07 / $84:8EEA). Only the player's matters to Zero. */
void MmxX2ZeroAnimationStart(unsigned object, unsigned sequence) {
  if (!MmxZeroEnabled() || object != X2_PLAYER || !MmxX2CoopLiveOwnsZero()) return;
  const unsigned x1 = sequence < sizeof(kX2ToX1Sequence) ? kX2ToX1Sequence[sequence] : 0xff;
  if (x1 != 0xff) {
    MmxZeroAnimationStart(X1_PLAYER, x1);
  } else {
    MmxZeroState s = MmxZeroGetState();
    s.anim_valid = 0;
    MmxZeroSelectState(s);
  }
}
void MmxX2ZeroAnimationAdvance(unsigned object) {
  if (MmxZeroEnabled() && object == X2_PLAYER && MmxX2CoopLiveOwnsZero())
    MmxZeroAnimationAdvance(X1_PLAYER);
}

/* $80:D0A8: a player shot's update starts (X1 $80:D3E5). Zero's own
 * transient objects (the saber's hit object) have no native update. */
unsigned MmxX2ZeroWeaponTick(unsigned object, unsigned active) {
  const unsigned d = to_x1(object);
  if (!zero_live() || !d) return active;
  uint8_t *v = build_view();
  const unsigned value = MmxZeroWeaponTick(v, d, active);
  write_back();
  return value;
}

/* $88:D86D: the hitbox pointer of a player shot (X1 $84:9C16). */
/* Zero's hitbox records live in X1's cart, bank $86 (DB of the contact
 * code in both games): saber arcs in $86:FF40..FF67 and $86:FFB0..FFFF, the
 * KSX move box at $86:FA44 (MmxZeroSetCollisionRom, mmx_saber_attack.c).
 * X2's bank $86 is free from $FD3A on, so the arcs go to the same addresses
 * in X2's cart and the KSX box to $86:FE00; X2's own $FA44 holds data. */
enum { BANK86 = 6 * 0x8000, X2_KSX_BOX = 0xfe00, X1_KSX_BOX = 0xfa44, BOX_SIZE = 40 };

static bool x2_rom_free(const uint8_t *rom, unsigned addr, unsigned size) {
  for (unsigned i = 0; i < size; ++i)
    if (rom[BANK86 + (addr - 0x8000) + i] != 0xff) return false;
  return true;
}

static unsigned publish_bounds(unsigned pointer) {
  static bool checked, usable;
  if (!x1_rom || x1_rom_size < BANK86 + 0x8000 || !g_snes || !g_snes->cart ||
      g_snes->cart->romSize < BANK86 + 0x8000) return pointer;
  uint8_t *x2_rom = g_snes->cart->rom;
  if (!checked) {
    /* Once, against the untouched cart: only ever write into X2's padding. */
    checked = true;
    usable = x2_rom_free(x2_rom, 0xff40, 0xc0) && x2_rom_free(x2_rom, X2_KSX_BOX, BOX_SIZE);
    if (!usable) fprintf(stderr, "[x2zero] X2 bank $86 padding not free; saber boxes stay X2's\n");
  }
  if (!usable) return pointer;
  if (pointer >= X1_KSX_BOX && pointer < X1_KSX_BOX + BOX_SIZE) {
    memcpy(x2_rom + BANK86 + (X2_KSX_BOX - 0x8000), x1_rom + BANK86 + (X1_KSX_BOX - 0x8000), BOX_SIZE);
    return X2_KSX_BOX + (pointer - X1_KSX_BOX);
  }
  if ((pointer >= 0xff40 && pointer < 0xff68) || pointer >= 0xffb0)
    memcpy(x2_rom + BANK86 + (0xff40 - 0x8000), x1_rom + BANK86 + (0xff40 - 0x8000), 0xc0);
  return pointer;
}

static void publish_all_bounds(void) {
  (void)publish_bounds(X1_KSX_BOX);
  (void)publish_bounds(0xff40);
}

unsigned MmxX2ZeroHitbox(unsigned enemy, unsigned projectile, unsigned original) {
  const unsigned e = to_x1(enemy), p = to_x1(projectile);
  if (debug_on()) {
    static unsigned n;
    if (projectile != X2_PLAYER && n++ < 40)
      fprintf(stderr, "[x2zero] hitbox call enemy=%04X shot=%04X ptr=%04X live=%d cls=%02X tag=%04X st=%02X/%02X\n",
              enemy, projectile, original, zero_live(), g_ram[projectile + 10],
              word(g_ram + projectile + 0x3e), g_ram[projectile + 1], g_ram[projectile + 2]);
  }
  if (!zero_live() || !e || !p) return original;
  uint8_t *v = build_view();
  const unsigned pointer = MmxWeaponsHitbox(v, e, p, MmxZeroHitbox(v, e, p, original));
  /* Zero's own hit objects carry their bounds pointer in +$20 already, so
   * the hooks usually return it unchanged: publish whenever the shot is
   * Zero's (+$3E tag: $53xx saber, $56xx wave, $5A53 X3 finisher), not only
   * when a hook changed the pointer. */
  const unsigned tag = word(v + p + 0x3e) >> 8;
  const bool own = tag == 0x53 || tag == 0x56 || tag == 0x5a;
  const unsigned result = own || pointer != original ? publish_bounds(pointer) : pointer;
  /* The box test reloads the shot's +$20 ($88:D89B) instead of using this
   * value: give the shot the published pointer so that read finds X2's copy
   * too (KSX's $FA44 is X2's own data in X2's cart). Zero's code puts its own
   * pointer back each frame. */
  if (result >= X2_KSX_BOX && result < X2_KSX_BOX + BOX_SIZE) {
    g_ram[projectile + 0x20] = (uint8_t)result;
    g_ram[projectile + 0x21] = (uint8_t)(result >> 8);
  }
  if (debug_on() && own && g_snes && g_snes->cart) {
    const uint8_t *b = g_snes->cart->rom + BANK86 + ((result - 0x8000) & 0x7fff);
    fprintf(stderr, "[x2zero] hitbox enemy=%04X shot=%04X ptr %04X -> %04X box %d,%d %u,%u enemy %u,%u shot %u,%u\n",
            enemy, projectile, original, result, (int8_t)b[0], (int8_t)b[1], b[2], b[3],
            word(g_ram + enemy + 5), word(g_ram + enemy + 8), word(g_ram + projectile + 5),
            word(g_ram + projectile + 8));
  }
  return result;
}

/* $88:DA9B..$88:DB09: a player shot touching an enemy (X1 $84:9E15..9E73).
 * Contact class (which weapon the shot counts as, KSX skills), the reaction
 * byte, the enemy's response and the damage, as X1's hooks do. */
unsigned MmxX2ZeroContactClass(unsigned enemy, unsigned projectile, unsigned original,
                               bool reaction) {
  const unsigned e = to_x1(enemy), p = to_x1(projectile);
  if (!zero_live() || !e || !p) return original;
  uint8_t *v = build_view();
  const unsigned value = MmxWeaponsContactClass(v, e, p, original, reaction);
  write_back();
  return value;
}
unsigned MmxX2ZeroResponse(unsigned enemy, unsigned projectile, unsigned original) {
  const unsigned e = to_x1(enemy), p = to_x1(projectile);
  if (!zero_live() || !e || !p) return original;
  uint8_t *v = build_view();
  const unsigned value = MmxZeroResponse(v, e, p, original);
  write_back();
  return value;
}
unsigned MmxX2ZeroDamage(unsigned enemy, unsigned projectile, unsigned original) {
  const unsigned e = to_x1(enemy), p = to_x1(projectile);
  if (!zero_live() || !e || !p) return original;
  uint8_t *v = build_view();
  const unsigned value = MmxWeaponsDamage(v, e, p, MmxZeroDamage(v, e, p, original));
  if (debug_on())
    fprintf(stderr, "[x2zero] damage enemy=%04X shot=%04X %u -> %u (enemy %u,%u shot %u,%u ptr %04X)\n",
            enemy, projectile, original, value, word(g_ram + enemy + 5), word(g_ram + enemy + 8),
            word(g_ram + projectile + 5), word(g_ram + projectile + 8), word(g_ram + projectile + 0x20));
  write_back();
  return value;
}

static void set_a8(CpuState *cpu, unsigned value) {
  cpu->A = (uint16_t)((cpu->A & 0xff00) | (value & 0xff));
  cpu->_flag_Z = !(value & 0xff);
  cpu->_flag_N = (value & 0x80) != 0;
  cpu->P = (uint8_t)((cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 0x80 : 0));
}

/* Interpreter-tier copies of the generated-code hooks: X2 runs whatever its
 * AOT coverage misses through the interpreter, where only pre-opcode hooks
 * see it. A site runs on one tier or the other, never both. */
static void set_nz16(CpuState *cpu, unsigned value) {
  cpu->_flag_Z = !(value & 0xffff);
  cpu->_flag_N = (value & 0x8000) != 0;
  cpu->P = (uint8_t)((cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 0x80 : 0));
}

static void interp_hook(CpuState *cpu, uint32_t pc) {
  switch (pc & 0x7fffff) {
    /* Shot-vs-enemy collision ($88:D7AC loop): X2 runs it on the interpreter,
     * so each generated-code hook has an instruction-level twin here, placed
     * just after the instruction whose result it adjusts. */
    case 0x08d870: { /* after LDY $0020,X (16-bit): the shot's hitbox pointer */
      const unsigned y = MmxX2ZeroHitbox(cpu->D, cpu->X, cpu->Y);
      cpu->Y = (uint16_t)y;
      set_nz16(cpu, y);
      break;
    }
    case 0x08d89e: /* after LDY $0020,X again: the record the overlap test reads */
      /* $88:D89B reloads the shot's +$20 for the box test itself, so KSX's
       * record (X1 $86:FA44) is read where it was published in X2's cart;
       * X2's own $FA44 holds other data. */
      if (zero_live() && cpu->Y >= X1_KSX_BOX && cpu->Y < X1_KSX_BOX + BOX_SIZE) {
        cpu->Y = (uint16_t)(X2_KSX_BOX + (cpu->Y - X1_KSX_BOX));
        set_nz16(cpu, cpu->Y);
      }
      if (debug_on() && g_snes && g_snes->cart) {
        const uint8_t *rom = g_snes->cart->rom;
        const unsigned ep = word(g_ram + cpu->D + 0x20);
        const uint8_t *sb = rom + BANK86 + ((cpu->Y - 0x8000) & 0x7fff);
        const uint8_t *eb = rom + BANK86 + ((ep - 0x8000) & 0x7fff);
        fprintf(stderr, "[x2zero] overlap D=%04X X=%04X DB=%02X shot box %04X %d,%d,%u,%u enemy box %04X %d,%d,%u,%u\n",
                cpu->D, cpu->X, cpu->DB, cpu->Y, (int8_t)sb[0], (int8_t)sb[1], sb[2], sb[3], ep,
                (int8_t)eb[0], (int8_t)eb[1], eb[2], eb[3]);
      }
      break;
    case 0x08daa3: { /* after LDA $000A,X (16-bit): the shot's contact class */
      const unsigned a = MmxX2ZeroContactClass(cpu->D, cpu->X, cpu->A, false);
      cpu->A = (uint16_t)a;
      set_nz16(cpu, a);
      break;
    }
    case 0x08dacc: /* after STA $1F2F: the reaction byte */
      g_ram[0x1f2f] = (uint8_t)MmxX2ZeroContactClass(cpu->D, cpu->X, g_ram[0x1f2f], true);
      break;
    case 0x08dad7: /* after LDA $F3A4,Y (8-bit): the enemy's response */
      set_a8(cpu, MmxX2ZeroResponse(cpu->D, cpu->X, cpu->A & 255));
      break;
    case 0x08db05: { /* after SBC $F3A4,Y (8-bit): HP minus the damage value */
      const unsigned hp = g_ram[(cpu->D + 0x27) & 0xffff] & 0x7f;
      const unsigned native = (hp - (cpu->A & 255)) & 255;
      const unsigned damage = MmxX2ZeroDamage(cpu->D, cpu->X, native) & 255;
      if (damage != native) {
        const unsigned a = (hp - damage) & 255;
        set_a8(cpu, a);
        cpu->_flag_C = hp >= damage;
        cpu->P = (uint8_t)((cpu->P & ~0x01) | (cpu->_flag_C ? 1 : 0));
      }
      break;
    }
    case 0x089479: MmxX2ZeroPrePlayer(); break;
    case 0x08947e: MmxX2ZeroPlayerEnd(); break;
    case 0x08c690: MmxX2ZeroAnimationStart(cpu->D, cpu->A & 255); break;
    case 0x08c673: MmxX2ZeroAnimationAdvance(cpu->D); break;
    /* $80:D0AA BEQ after LDA $00 of a player shot ($80:D0A2 loop). */
    case 0x00d0aa: set_a8(cpu, MmxX2ZeroWeaponTick(cpu->D, cpu->A & 255)); break;
  }
}

static void weapon_switch_hook(CpuState *cpu, uint32_t pc);
static void register_hooks(void) {
  static const uint32_t sites[] = {0x089479, 0x08947e, 0x08c690, 0x08c673, 0x00d0aa,
                                   0x08d870, 0x08d89e, 0x08daa3, 0x08dacc, 0x08dad7, 0x08db05};
  interp_bridge_set_pre_opcode_hook_owner(1);
  for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(sites[i], interp_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x08bed3, weapon_switch_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
}

/* Terrain for Zero's own moves (Ryuenjin's ceiling, KSX's walls and
 * floors): X2's map, read as X1's is ($84:96xx) at X2's addresses. Screens
 * $E800, 16x16 blocks from $2800, the collision class table at $09C2 (X1
 * $0B92), classes in X2's cart; X2 keeps X1's class numbers. */
static unsigned x2_terrain_type(int x, int y) {
  if (x < 0 || x >= 8192 || y < 0 || y >= 8192 || !g_snes || !g_snes->cart) return 0;
  const unsigned screen = g_ram[0xe800 + (y >> 8) * 32 + (x >> 8)];
  const unsigned cell = 0x2800 + ((screen * 512 + ((y & 240) << 1) + ((x & 240) >> 3)) & 0xffff);
  if (cell + 1 >= 0x20000) return 0;
  unsigned address = word(g_ram + 0x9c2) | (unsigned)g_ram[0x9c4] << 16;
  address += word(g_ram + cell);
  if ((address & 0xffff) < 0x8000) return 0;
  const size_t offset = ((address >> 16) & 127) * 0x8000u + (address & 0x7fff);
  return offset < g_snes->cart->romSize ? g_snes->cart->rom[offset] & 63 : 0;
}

void MmxX2ZeroEnter(uint8_t *rom, size_t size) {
  MmxWeaponsSetTerrainSource(x2_terrain_type);
  x1_rom = rom;
  x1_rom_size = size;
  if (!MmxZeroEnabled()) return;
  register_hooks();
  /* X1's character state stays X1's; X2 has its own, and starts with X. */
  x1_state = MmxZeroGetState();
  x1_body_hooks = MmxZeroGetBodyHooks();
  MmxZeroSetBodyHooks(NULL);
  if (!x2_state_valid) {
    MmxZeroResetState();
    x2_state = MmxZeroGetState();
    x2_state.active_x = 1;
    x2_state_valid = true;
  }
  MmxZeroSetState(x2_state);
  previous_pad = 0xffff;
  /* Co-op: one Zero seat owns the KSX extension, by its own state. */
  x1_gate = MmxZeroGetExtensionGate();
  MmxX2CoopRegisterHooks();
  MmxX2CoopEnter();
  if (MmxX2CoopActive()) MmxZeroSetExtensionGate(MmxZeroActive);
}

/* X2 snapshots (x2_runtime.c's X2StateSaveExtra): Zero's live state and
 * KSX's extension, the exchange's edge and co-op's seats, so rollback,
 * rewind and save states restore them with X2's machine. X1's own state
 * (x1_state) is X1's snapshot's. A program swap restores X2's machine before
 * MmxX2ZeroEnter, which keeps its own copy, so a load during a swap is
 * skipped. */
enum { BRIDGE_HEAD = 16 };
/* The shared items' tracking (mmx_x2share.c) and OPTION MODE's double-tap
 * counters run on X2's frames too, so X2's snapshots carry them: a replay or
 * a save state replays them exactly. */
static size_t tail_size(void) { return MmxX2ShareBlob(NULL, 0) + sizeof(MmxOptionsState); }
static size_t zero_part(void) {
  return sizeof(MmxZeroState) + MMX_ZERO_EXTENSION_STATE_BYTES + MmxX2CoopStateSize();
}
size_t MmxX2BridgeStateSize(void) {
  return BRIDGE_HEAD + zero_part() + tail_size();
}

void MmxX2BridgeSaveState(uint8_t *out) {
  memset(out, 0, BRIDGE_HEAD);
  out[0] = MmxZeroEnabled() ? 1 : 0;
  out[1] = select_edge ? 1 : 0;
  out[2] = (uint8_t)previous_pad; out[3] = (uint8_t)(previous_pad >> 8);
  /* X1 to X2 boss weapons: the pause page and its last L / R. */
  { const uint16_t page = MmxX2PauseState(); out[4] = (uint8_t)page; out[5] = (uint8_t)(page >> 8); }
  /* X2 game extension: the Xtreme Sub Tanks' energy. */
  MmxX2ShareXtremeSave(out + 6);
  out[10] = MmxX2ShareX1Armor();
  { const uint16_t hidden = MmxX2PauseZeroHidden(); out[11] = (uint8_t)hidden; out[12] = (uint8_t)(hidden >> 8); }
  uint8_t *p = out + BRIDGE_HEAD;
  MmxZeroState zero;
  memset(&zero, 0, sizeof(zero));
  if (MmxZeroEnabled()) zero = MmxZeroGetState();
  memcpy(p, &zero, sizeof(zero));
  p += sizeof(zero);
  memset(p, 0, MMX_ZERO_EXTENSION_STATE_BYTES);
  if (MmxZeroEnabled()) MmxZeroExtSaveState(p);
  p += MMX_ZERO_EXTENSION_STATE_BYTES;
  MmxX2CoopSaveState(p);
  p = out + BRIDGE_HEAD + zero_part();
  p += MmxX2ShareBlob(p, MmxX2ShareBlob(NULL, 0));
  const MmxOptionsState options = MmxOptionsGetState();
  memcpy(p, &options, sizeof(options));
}

void MmxX2BridgeLoadState(const uint8_t *in, size_t size) {
  if (size == MmxX2BridgeStateSize() && !MmxX2ExtSwapping()) {
    MmxX2PauseSetState((uint16_t)(in[4] | in[5] << 8));
    MmxX2ShareSetX1Armor(in[10]);
    MmxX2PauseSetZeroHidden((uint16_t)(in[11] | in[12] << 8));
    const uint8_t *tail = in + BRIDGE_HEAD + zero_part();
    const size_t share = MmxX2ShareBlob(NULL, 0);
    (void)MmxX2ShareLoadState(tail, share);
    MmxOptionsState options;
    memcpy(&options, tail + share, sizeof(options));
    (void)MmxOptionsSetState(&options);
  }
  if (size != MmxX2BridgeStateSize() || !in[0] || !MmxZeroEnabled() ||
      MmxX2ExtSwapping()) return;
  select_edge = in[1] != 0;
  previous_pad = (uint16_t)(in[2] | in[3] << 8);
  const uint8_t *p = in + BRIDGE_HEAD;
  MmxZeroState zero;
  memcpy(&zero, p, sizeof(zero));
  p += sizeof(zero);
  MmxZeroSelectState(zero);
  if (!MmxZeroExtLoadState(p)) MmxZeroSetState(zero);
  p += MMX_ZERO_EXTENSION_STATE_BYTES;
  (void)MmxX2CoopLoadState(p, MmxX2CoopStateSize());
}

void MmxX2ZeroLeave(void) {
  MmxWeaponsSetTerrainSource(NULL);
  if (!MmxZeroEnabled()) return;
  if (MmxX2CoopActive()) {
    MmxX2CoopLeave();
    MmxZeroSetExtensionGate(x1_gate);
  }
  x2_state = MmxZeroGetState();
  MmxZeroSetState(x1_state);
  MmxZeroSetBodyHooks(x1_body_hooks);
}

static bool world_held;
bool MmxX2ZeroWorldHeld(void) { return world_held; }

bool MmxX2ZeroFrameBegin(uint32_t inputs) {
  world_held = false;
  if (!MmxZeroEnabled()) return true;
  /* X1's host does this every frame (main.c MmxBeforeFrame): Zero's and
   * KSX's hitbox records are armed in X1's cart only while Zero is the
   * active body. They stay in X1's cart; the hitbox hook copies them into
   * X2's (publish_bounds). */
  if (x1_rom) MmxZeroSetCollisionRom(x1_rom, x1_rom_size);
  /* Co-op has no character exchange: each seat plays its own character. */
  if (MmxX2CoopActive()) {
    MmxX2CoopFrameBegin();
    return true;
  }
  const uint16_t pad = (uint16_t)(inputs & 0xfff);
  select_edge = (pad & ~previous_pad & (1u << 2)) != 0;
  previous_pad = pad;
  if (!x2_gameplay() && !MmxZeroSwapping()) return true;
  build_view();
  /* The exchange reads Select's new-press bit ($AC = $20) before X2's NMI
   * has polled this frame; take it from the host's pad instead. */
  view[0xab] = 0;
  view[0xac] = select_edge ? 0x20 : 0;
  world_held = MmxZeroSwapTick(view);
  write_back();
  if (debug_on()) {
    static bool was;
    static unsigned since;
    if (select_edge) fprintf(stderr, "[x2zero] Select pressed (held=%d act=%02X/%02X)\n",
                             world_held, g_ram[X2_PLAYER + 2], g_ram[X2_PLAYER + 3]);
    if (world_held != was) {
      fprintf(stderr, "[x2zero] exchange %s%s\n", world_held ? "starts" : "ends after ",
              world_held ? "" : "");
      if (!world_held) fprintf(stderr, "[x2zero]   %u frames held\n", since);
      since = 0;
    }
    if (world_held) ++since;
    was = world_held;
  }
  /* The exchange holds the game still, as in X1: the frame runs its NMI
   * only (x2_runtime.c), so music, input and the screen keep going. */
  return true;
}

/* X's sprites: OBJ palette 1 with the player's VRAM tiles $00-$1F. */
static bool x_sprite(unsigned i) {
  const unsigned attr = g_ppu->oam[i * 2 + 1] >> 8, tile = g_ppu->oam[i * 2 + 1] & 255;
  return !(attr & 1) && ((attr >> 1) & 7) == 1 && tile < 0x20;
}

/* The health bar's "X" badge: tile $86, palette $34, at the bar's left
 * (x2_rtl.c's HUD survey). Zero shows X3's Z badge there instead. */
static bool hud_badge(unsigned i) {
  return (g_ppu->oam[i * 2 + 1] & 0xff) == 0x86 && (g_ppu->oam[i * 2 + 1] >> 8) == 0x34 &&
      (g_ppu->oam[i * 2] & 0xff) == 8;
}

static uint16_t hidden_y[128];
static bool hidden[128];
static int badge_x = -1, badge_y = -1;

/* Zero (and X's exchange beam) is not painted over the finished frame: it is
 * composed by the PPU as one host object (PpuSetExtraObjects) at X's own OBJ
 * priority and OAM slot, so walls and foreground tiles that cover X cover him
 * too, and fades, windows and brightness apply. canvas is the screen, row r
 * being scanline r + 1, column CANVAS_X0 + x screen x (widescreen margins:
 * x < 0 and x >= 256 too); bit 15 marks an opaque BGR555 pixel. */
enum { CANVAS_W = kPpuBufWidth, CANVAS_X0 = kPpuExtraLeftRight };
static uint16_t canvas[224 * CANVAS_W];
static bool canvas_used;
static PpuExtraObject canvas_object;

/* The drawn field's margins (g_ppu: the field, or player 2's pane). */
static int view_left(void) { return g_ppu ? g_ppu->extraLeftCur : 0; }
static int view_right(void) { return g_ppu ? g_ppu->extraRightCur : 0; }

static void canvas_clear(void) {
  memset(canvas, 0, sizeof(canvas));
  canvas_used = false;
}

static void put(int x, int y, uint16_t color) {
  if (x < -view_left() || x >= 256 + view_right() || y < 0 || y >= 224) return;
  canvas[y * CANVAS_W + CANVAS_X0 + x] = (uint16_t)(color | 0x8000);
  canvas_used = true;
}

/* The canvas as one host object over the drawn field. */
static void canvas_publish(unsigned priority, int slot) {
  const int left = view_left();
  canvas_object = (PpuExtraObject){
      .x = -left, .y = 1, .width = (uint16_t)(256 + left + view_right()), .height = 224,
      .stride = CANVAS_W, .priority = (uint8_t)priority, .math = 0,
      .oamSlot = (uint8_t)(slot < 0 ? 0 : slot), .order = 1,
      .pixels = canvas + CANVAS_X0 - left};
  PpuSetExtraObjects(g_ppu, &canvas_object, 1);
}

/* As mmx_renderer.c's player_overlay_plane_row, over the whole plane. */
static void draw_plane(const MmxRenderPlayerOverlayPlane *plane, const uint16_t *palette,
                       unsigned palette_count, int zx, int zy, bool mirror) {
  if (!plane || !plane->pixels || !palette || !palette_count) return;
  for (int row = 0; row < plane->height; ++row) {
    const int y = zy - 64 + plane->origin_y + row;
    for (int col = 0; col < plane->width; ++col) {
      const unsigned pixel = plane->pixels[row * plane->width + col];
      if (!pixel || pixel >= palette_count) continue;
      const int canvas_col = plane->origin_x + col;
      put(zx + (mirror ? 63 - canvas_col : canvas_col - 64), y, palette[pixel]);
    }
  }
}

/* As mmx_renderer.c's world_sprite_row (saber waves, KSX effects). */
static void draw_world_sprites(const uint8_t *v) {
  MmxRendererWorldSpriteProvider provider = MmxRendererGetWorldSpriteProvider();
  if (!provider) return;
  MmxRenderWorldSprite sprites[32];
  const unsigned count = provider(sprites, 32);
  const int cx = (int)word(v + 0x1e4d), cy = (int)word(v + 0x1e50);
  for (unsigned i = 0; i < count; ++i) {
    const MmxRenderWorldSprite *sp = &sprites[i];
    if (!sp->pixels || !sp->palette || !sp->palette_count) continue;
    const int sx = (int)sp->world_x - cx, sy = (int)sp->world_y - cy;
    for (int row = 0; row < sp->height; ++row)
      for (int col = 0; col < sp->width; ++col) {
        const unsigned pixel = sp->pixels[row * sp->width + col];
        if (!pixel || pixel >= sp->palette_count) continue;
        put(sx + (sp->facing_left ? -1 - sp->origin_x - col : sp->origin_x + col),
            sy + sp->origin_y + row, sp->palette[pixel]);
      }
  }
}

/* X's exchange beam in X2's own art: X2's beam-in/out poses ($3C traveling
 * column, $3D..$42 morphs; X2 sequences $6F..$71) built from X2's ROM.
 * Pose tiles come from the player's 6-byte CHR records (pointer +$31, data
 * bank $05: $05:9609; $88:C9C3), the arrangement from $8D:8000 group 0
 * ($80:D45F), the colors from X's live OBJ palette 1. As in X1, the middle
 * morphs $3F..$41 show $3E's CHR. */
static size_t x2_lorom(unsigned address) {
  return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
}

static bool x2_rom_range(const uint8_t *rom, size_t size, size_t at, size_t count) {
  return rom && at <= size && count <= size - at;
}

static bool x2_beam_canvas(unsigned pose, bool flip, uint16_t *out) {
  static uint8_t tiles[8192];
  if (!g_snes || !g_snes->cart || !g_ppu) return false;
  const uint8_t *rom = g_snes->cart->rom;
  const size_t size = g_snes->cart->romSize;
  /* CHR: X's pose records, base from the player's +$31 in bank $05. */
  const unsigned base = x2_lorom(0x050000 | (g_ram[X2_PLAYER + 0x31] | g_ram[X2_PLAYER + 0x32] << 8));
  unsigned f = pose >= 0x3f && pose <= 0x41 ? 0x3e : pose;
  if (!x2_rom_range(rom, size, base + f * 2, 2)) return false;
  size_t dma = base + (rom[base + f * 2] | rom[base + f * 2 + 1] << 8);
  memset(tiles, 0, sizeof(tiles));
  bool complete = false;
  /* X2's records are six bytes (X1's five): 16-byte count, 24-bit source,
   * 16-bit VRAM word destination whose top bit ends the list. */
  for (unsigned guard = 0; guard < 32; ++guard, dma += 6) {
    if (!x2_rom_range(rom, size, dma, 6) || !rom[dma]) return false;
    const unsigned count = rom[dma] * 16;
    const size_t source = x2_lorom(rom[dma + 1] | rom[dma + 2] << 8 | rom[dma + 3] << 16);
    const unsigned vram = (rom[dma + 4] | rom[dma + 5] << 8) & 0x7fff;
    const int dest = ((int)vram - 0x6000) * 2;
    if (!x2_rom_range(rom, size, source, count) || dest < 0 || dest + (int)count > (int)sizeof(tiles))
      return false;
    memcpy(tiles + dest, rom + source, count);
    if (rom[dma + 5] & 128) { complete = true; break; }
  }
  if (!complete) return false;
  /* Arrangement: $8D:8000 + group*3 -> group table -> pose record. */
  const size_t group = x2_lorom(0x8d8000);
  if (!x2_rom_range(rom, size, group, 3)) return false;
  const unsigned table = rom[group] | rom[group + 1] << 8 | rom[group + 2] << 16;
  const size_t entry = x2_lorom(table + pose * 3);
  if (!x2_rom_range(rom, size, entry, 3)) return false;
  const size_t arrangement = x2_lorom(rom[entry] | rom[entry + 1] << 8 | rom[entry + 2] << 16);
  if (!x2_rom_range(rom, size, arrangement, 1) ||
      !x2_rom_range(rom, size, arrangement, 1 + rom[arrangement] * 4u)) return false;
  const uint8_t *layout = rom + arrangement;
  const uint16_t *palette = g_ppu->cgram + 144;  /* OBJ palette 1 */
  memset(out, 0, 128 * 128 * sizeof(*out));
  for (int i = layout[0] - 1; i >= 0; --i) {
    /* X2's pieces are (flags, x, y, tile); X1's are (x, y, tile, flags). */
    const uint8_t *e = layout + 1 + i * 4;
    const uint8_t p[5] = {0, e[1], e[2], e[3], e[0]};
    const int piece = p[4] & 0x20 ? 16 : 8;
    const int px = flip ? -(int8_t)p[1] - piece : (int8_t)p[1];
    const int py = (int8_t)p[2];
    const bool hflip = ((p[4] & 0x40) != 0) != flip, vflip = (p[4] & 0x80) != 0;
    for (int ty = 0; ty < piece; ++ty)
      for (int tx = 0; tx < piece; ++tx) {
        const int sx = hflip ? piece - 1 - tx : tx, sy = vflip ? piece - 1 - ty : ty;
        const unsigned tile = (p[3] + (sy / 8) * 16 + sx / 8) & 255;
        const uint8_t *t = tiles + tile * 32 + (sy & 7) * 2;
        const unsigned bit = 7 - (sx & 7);
        const unsigned pixel = ((t[0] >> bit) & 1) | ((t[1] >> bit) & 1) << 1 |
            ((t[16] >> bit) & 1) << 2 | ((t[17] >> bit) & 1) << 3;
        const int cx = 64 + px + tx, cy = 64 + py + ty;
        if (pixel && cx >= 0 && cx < 128 && cy >= 0 && cy < 128)
          out[cy * 128 + cx] = (uint16_t)(palette[pixel] | 0x8000);
      }
  }
  return true;
}

/* Zero's body, KSX planes, saber effects, or either character's exchange
 * beam, into canvas. */
static void draw_body(const uint8_t *v, const MmxZeroState *s) {
  const int px = (int16_t)(word(v + 0x0bad) - word(v + 0x1e4d));
  const int py = (int16_t)(word(v + 0x0bb0) - word(v + 0x1e50));
  const bool flip = (v[0x0bb9] & 64) != 0;
  if (s->swap_phase) {
    const unsigned pose = MmxZeroSwapPose(s);
    if (pose == 255) return;
    if (s->active_x) {
      /* X's own beam: X2's beam-in/out art (x2_beam_canvas). */
      static uint16_t beam[128 * 128];
      if (!x2_beam_canvas(pose, flip, beam)) return;
      for (int row = 0; row < 128; ++row)
        for (int col = 0; col < 128; ++col)
          if (beam[row * 128 + col])
            put(px - 64 + col, py + s->swap_y - 64 + row, beam[row * 128 + col]);
      return;
    }
    const uint8_t *pixels = MmxZeroTeleportPose(pose);
    const uint16_t *colors = MmxZeroColors();
    if (!pixels) return;
    const int zy = py - 8 + s->swap_y;
    for (int row = 0; row < MMX_ZERO_HEIGHT; ++row)
      for (int col = 0; col < MMX_ZERO_WIDTH; ++col) {
        const unsigned pixel = pixels[row * MMX_ZERO_WIDTH + col];
        if (pixel) put(px + (flip ? 63 - col : col - 64), zy - 64 + row, colors[pixel]);
      }
    return;
  }
  const int zy = py + MmxZeroPoseOffsetY(v);
  /* KSX Zero's attacks draw their own body and blade planes. */
  MmxRendererPlayerOverlayProvider provider = MmxRendererGetPlayerOverlayProvider();
  MmxRenderPlayerOverlay overlay;
  memset(&overlay, 0, sizeof(overlay));
  if (provider && provider(v, s, &overlay) && overlay.active) {
    if (overlay.blade_layer == 1)
      draw_plane(&overlay.blade, overlay.palette, overlay.palette_count, px, zy, overlay.facing_left);
    draw_plane(&overlay.body, overlay.palette, overlay.palette_count, px, zy, overlay.facing_left);
    if (overlay.blade_layer == 2)
      draw_plane(&overlay.blade, overlay.palette, overlay.palette_count, px, zy, overlay.facing_left);
    draw_world_sprites(v);
    return;
  }
  draw_world_sprites(v);
  const uint8_t *body = MmxZeroPose(v, s);
  if (!body) return;
  const uint8_t *blade = MmxZeroBlade(s);
  const uint16_t *colors = MmxZeroColors();
  const uint16_t *body_colors = MmxZeroBodyColors(s);
  for (int row = 0; row < MMX_ZERO_HEIGHT; ++row)
    for (int col = 0; col < MMX_ZERO_WIDTH; ++col) {
      unsigned pixel = body[row * MMX_ZERO_WIDTH + col];
      if (blade && blade[row * MMX_ZERO_WIDTH + col]) pixel = blade[row * MMX_ZERO_WIDTH + col];
      if (!pixel) continue;
      put(px + (flip ? 63 - col : col - 64), zy - 64 + row,
          pixel >= 16 && pixel < 32 ? body_colors[pixel - 16] : colors[pixel]);
    }
}

static int coop_badge_x = -1, coop_badge_y = -1;

/* Co-op: player 1 is X2's own X; Zero's seat is drawn from its stored body
 * at X's OBJ priority, and player 1's badge anchors player 2's meter. */
static void coop_before_draw(void) {
  int slot = -1;
  unsigned priority = 2;
  for (unsigned i = 0; i < 128; ++i) {
    if (hud_badge(i) && (g_ppu->oam[i * 2] >> 8) < 224) {
      coop_badge_x = g_ppu->oam[i * 2] & 0xff;
      coop_badge_y = g_ppu->oam[i * 2] >> 8;
    } else if (x_sprite(i) && (g_ppu->oam[i * 2] >> 8) < 224) {
      /* X's last piece: Zero sorts behind every piece of X (as in X1
       * co-op, X is always in front). */
      slot = (int)i;
      priority = (g_ppu->oam[i * 2 + 1] >> 12) & 3;
    }
  }
  if (!MmxX2CoopLiveIsX()) return;
  MmxZeroState s;
  if (!MmxX2CoopDrawnSeat(&s, NULL)) return;
  canvas_clear();
  draw_body(build_view(), &s);
  if (!canvas_used) return;
  canvas_publish(priority, (slot < 0 ? 0 : slot));
}

/* Side by side, player 2's pane (g_ppu is that pane, its OAM already moved
 * to its camera): Zero's seat drawn for that camera. */
void MmxX2ZeroPaneObjects(int camera_x, int camera_y) {
  if (!MmxZeroEnabled() || !g_ppu || !x2_gameplay() || !MmxX2CoopActive() ||
      !MmxX2CoopLiveIsX()) return;
  int slot = -1;
  unsigned priority = 2;
  for (unsigned i = 0; i < 128; ++i)
    if (x_sprite(i) && (g_ppu->oam[i * 2] >> 8) < 224) {
      slot = (int)i;
      priority = (g_ppu->oam[i * 2 + 1] >> 12) & 3;
    }
  MmxZeroState s;
  if (!MmxX2CoopDrawnSeat(&s, NULL)) return;
  canvas_clear();
  uint8_t *v = build_view();
  v[0x1e4d] = (uint8_t)camera_x; v[0x1e4e] = (uint8_t)(camera_x >> 8);
  v[0x1e50] = (uint8_t)camera_y; v[0x1e51] = (uint8_t)(camera_y >> 8);
  draw_body(v, &s);
  if (!canvas_used) return;
  canvas_publish(priority, (slot < 0 ? 0 : slot));
}

bool MmxX2ZeroCoopBadge(int *x, int *y) {
  *x = coop_badge_x; *y = coop_badge_y;
  return coop_badge_x >= 0;
}

/* X2's pause screen: X's figure at the top right is OBJ tiles $1E0..$1F7
 * (attr $33: priority 3, palette 1, second name table) above y 64. While
 * Zero is the one paused, it is hidden and Zero's own menu pose (X1's pause
 * Zero, MmxZeroMenuPose) stands in its place, feet on the same line. True
 * when the pause figure is on screen. */
static bool pause_figure(void) {
  int left = 1 << 20, right = -(1 << 20), bottom = -(1 << 20), slot = -1;
  bool found[128] = {0}, any = false;
  for (unsigned i = 0; i < 128; ++i) {
    const unsigned lo = g_ppu->oam[i * 2], hi = g_ppu->oam[i * 2 + 1];
    const unsigned y = lo >> 8, tile = hi & 255, attr = hi >> 8;
    if ((attr & 0x0f) != 0x03 || tile < 0xe0 || tile >= 0xf8 || y >= 64) continue;
    const unsigned high = (unsigned)(g_ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 3;
    int x = (int)(lo & 255) | (high & 1 ? 256 : 0);
    if (x >= 256) x -= 512;
    if (x < 160) continue;
    const int size = high & 2 ? 16 : 8;
    found[i] = any = true;
    if (slot < 0) slot = (int)i;
    if (x < left) left = x;
    if (x + size > right) right = x + size;
    if ((int)y + size > bottom) bottom = (int)y + size;
  }
  if (!any) return false;
  const MmxZeroState s = MmxZeroGetState();
  const bool zero = MmxX2CoopActive() ? !MmxX2CoopLiveIsX() : !s.active_x;
  const uint8_t *pose = MmxZeroMenuPose();
  if (!zero || !pose) return true;
  /* The pose's own extent (once). */
  static int px0 = -1, px1, py1;
  if (px0 < 0) {
    px0 = MMX_ZERO_WIDTH; px1 = py1 = 0;
    for (int row = 0; row < MMX_ZERO_HEIGHT; ++row)
      for (int col = 0; col < MMX_ZERO_WIDTH; ++col)
        if (pose[row * MMX_ZERO_WIDTH + col]) {
          if (col < px0) px0 = col;
          if (col > px1) px1 = col;
          if (row > py1) py1 = row;
        }
  }
  for (unsigned i = 0; i < 128; ++i) {
    if (!found[i]) continue;
    hidden[i] = true;
    hidden_y[i] = g_ppu->oam[i * 2];
    g_ppu->oam[i * 2] = (uint16_t)((g_ppu->oam[i * 2] & 0xff) | (0xf0 << 8));
  }
  canvas_clear();
  const int dx = (left + right) / 2 - (px0 + px1 + 1) / 2, dy = bottom - 1 - py1;
  const uint16_t *colors = MmxZeroColors();
  const uint16_t *body_colors = MmxZeroBodyColors(&s);
  for (int row = 0; row < MMX_ZERO_HEIGHT; ++row)
    for (int col = 0; col < MMX_ZERO_WIDTH; ++col) {
      const unsigned pixel = pose[row * MMX_ZERO_WIDTH + col];
      if (pixel) put(col + dx, row + dy, pixel >= 16 && pixel < 32 ? body_colors[pixel - 16] : colors[pixel]);
    }
  if (!canvas_used) return true;
  canvas_publish(3, slot);
  return true;
}

/* X2's weapon switch ($08:BED3, from $08:BECC: L / R, both for the buster,
 * Up + Down for $12): Zero has no X2 weapons for now (his X2 page skills
 * come later), so it never runs for him and he keeps no X2 weapon (+$33).
 * Nonzero skips the routine (to its RTS, $08:BECB). */
int MmxX2ZeroSkipWeaponSwitch(void) {
  if (!x2_gameplay() || !MmxX2ZeroPausedIsZero()) return 0;
  g_ram[X2_PLAYER + 0x33] = 0;
  return 1;
}
static void weapon_switch_hook(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc;
  if (MmxX2ZeroSkipWeaponSwitch()) interp_bridge_pre_opcode_redirect(0x08becb);
}

/* The one paused is Zero (his pause figure, his X1 skills page). */
bool MmxX2ZeroPausedIsZero(void) {
  if (!MmxZeroEnabled()) return false;
  if (MmxX2CoopActive()) return !MmxX2CoopLiveIsX();
  return !MmxZeroGetState().active_x;
}

void MmxX2ZeroBeforeDraw(void) {
  memset(hidden, 0, sizeof(hidden));
  badge_x = badge_y = -1;
  coop_badge_x = coop_badge_y = -1;
  if (!MmxZeroEnabled() || !g_ppu || !x2_gameplay()) return;
  /* The pause screen: Zero's own figure when he is the one paused. */
  if (pause_figure()) return;
  if (MmxX2CoopActive() && MmxX2CoopLiveIsX()) {
    coop_before_draw();
    return;
  }
  const MmxZeroState s = MmxZeroGetState();
  /* X with no exchange running is X2's own sprites. */
  if (s.active_x && !s.swap_phase) return;
  int slot = -1;
  unsigned priority = 2;
  for (unsigned i = 0; i < 128; ++i) {
    if (!s.active_x && hud_badge(i) && (g_ppu->oam[i * 2] >> 8) < 224) {
      badge_x = g_ppu->oam[i * 2] & 0xff;
      badge_y = g_ppu->oam[i * 2] >> 8;
    } else if (!x_sprite(i)) {
      continue;
    } else if (slot < 0) {
      slot = (int)i;
      priority = (g_ppu->oam[i * 2 + 1] >> 12) & 3;
    }
    hidden[i] = true;
    hidden_y[i] = g_ppu->oam[i * 2];
    g_ppu->oam[i * 2] = (uint16_t)((g_ppu->oam[i * 2] & 0xff) | (0xf0 << 8));
  }
  /* No X sprites this frame (an invulnerability blink): no body either. */
  if (slot < 0 && !s.swap_phase) return;
  canvas_clear();
  draw_body(build_view(), &s);
  if (!canvas_used) return;
  canvas_publish(priority, (slot < 0 ? 0 : slot));
}

static uint32_t screen_color(uint16_t c) {
  const uint8_t *m = g_ppu->brightnessMult;
  return (uint32_t)m[c & 31] << 16 | (uint32_t)m[(c >> 5) & 31] << 8 | m[(c >> 10) & 31];
}

static uint32_t *screen_row(int y) {
  /* Screen x 0 (the native view's left edge): the row holds extraLeftRight
   * columns before it (the PPU's compose offset plus extraLeftCur). */
  return (uint32_t *)(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch) +
      g_ppu->extraLeftRight;
}

void MmxX2ZeroAfterDraw(void) {
  if (!g_ppu) return;
  PpuSetExtraObjects(g_ppu, NULL, 0);
  for (unsigned i = 0; i < 128; ++i)
    if (hidden[i]) g_ppu->oam[i * 2] = hidden_y[i];
  if (MmxX2CoopActive()) MmxX2CoopDrawHud(coop_badge_x >= 0 ? coop_badge_x : badge_x,
                                          coop_badge_x >= 0 ? coop_badge_y : badge_y);
  if (badge_x < 0) return;
  /* The HUD badge is not world art: it goes over the finished frame.
   * A sprite at OAM Y starts on line Y+1, which is renderBuffer row Y.
   * Widescreen moves the bar it sits on out to the edge (X2ConfigureWsHud). */
  const int bx = badge_x - X2WsHudLeftShift();
  for (unsigned row = 0; row < 16; ++row) {
    const int y = badge_y + (int)row;
    if (y < 0 || y >= 224) continue;
    uint32_t *dst = screen_row(y);
    for (unsigned col = 0; col < 16; ++col) {
      const int color = MmxZeroHudColor(col, row);
      const int x = bx + (int)col;
      if (color >= 0 && x >= -view_left() && x < 256 + view_right())
        dst[x] = screen_color((uint16_t)color);
    }
  }
}

/* Developer mod: Hitbox overlay on the X2 page. X2's objects share X1's
 * layout and collision records (+$20 into bank $86), so X1's collector runs
 * on the X1-layout view with X2's cart; outlines go over the finished frame. */
static void draw_hitbox_view(void);
/* Co-op: each player's boxes (the view holds one player at a time). */
void MmxX2DrawHitboxOverlay(void) {
  if (!MmxHitboxOverlayEnabled() || !g_ppu || !g_snes || !g_snes->cart) return;
  uint8_t *const kept = player_src;
  player_src = NULL;
  draw_hitbox_view();
  uint8_t *other = MmxX2CoopOtherBody();
  if (other) {
    player_src = other;
    draw_hitbox_view();
  }
  player_src = kept;
}

static void draw_hitbox_view(void) {
  MmxRenderDebugRect rects[64];
  /* The overlay reads the same records X2's collision does, so KSX's box,
   * kept at X2 $86:FE00 (publish_bounds), is looked up there. */
  static uint8_t shown[0x20000];
  memcpy(shown, build_view(), sizeof(shown));
  for (unsigned d = 0x1228; d < 0x1428; d += 0x40) {
    const unsigned pointer = word(shown + d + 0x20);
    if (pointer >= X1_KSX_BOX && pointer < X1_KSX_BOX + BOX_SIZE) {
      const unsigned moved = X2_KSX_BOX + (pointer - X1_KSX_BOX);
      shown[d + 0x20] = (uint8_t)moved;
      shown[d + 0x21] = (uint8_t)(moved >> 8);
    }
  }
  const uint8_t *v = shown;
  const unsigned count = MmxHitboxOverlayCollect(v, g_snes->cart->rom, g_snes->cart->romSize,
                                                 NULL, NULL, rects, 64);
  const int cx = (int)word(v + 0x1e4d), cy = (int)word(v + 0x1e50);
  const int x_min = -view_left(), x_max = 256 + view_right();
  for (unsigned i = 0; i < count; ++i) {
    const MmxRenderDebugRect *r = &rects[i];
    const uint32_t color = screen_color(r->rgb555);
    const int left = (int)r->world_x - cx, top = (int)r->world_y - cy;
    const int right = left + r->w - 1, bottom = top + r->h - 1;
    for (int y = top; y <= bottom; ++y) {
      if (y < 0 || y >= 224) continue;
      uint32_t *dst = screen_row(y);
      if (y == top || y == bottom) {
        for (int x = left; x <= right; ++x) if (x >= x_min && x < x_max) dst[x] = color;
      } else {
        if (left >= x_min && left < x_max) dst[left] = color;
        if (right >= x_min && right < x_max) dst[right] = color;
      }
    }
  }
}
