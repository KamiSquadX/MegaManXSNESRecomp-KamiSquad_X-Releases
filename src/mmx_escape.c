/* Escape Bugs (Accessibility mod): commands on the pause menu's ESCAPE.U
 * (X1) / EXIT (X2) while it is highlighted, and one for anywhere in a stage
 * (see mmx_escape.h).
 *
 * Everything runs on the frame's inputs before the frame (MmxEscapeFrameBegin,
 * after replay playback), so replays and netplay see the same commands. The
 * two bytes it keeps ride in OPTION MODE's state (escape_hold/escape_flags),
 * which both games' snapshots already carry.
 *
 * While ESCAPE.U is highlighted, B, Start, Select and X are held back from
 * the game. A short B or Start is handed back to it on release (one frame),
 * so the menu does what it always did. Every command closes the menu (Start,
 * with the cursor back on the weapon it opened on), so its effect is seen:
 *   X, Select  the camera / the player moves as the menu closes (the close
 *          redraws the whole screen from the new camera).
 *   B (3 s)  once the menu has closed: the death state's own ending without
 *          a death (fade, reload at the checkpoint, or the stage's start).
 *   Start (3 s)  the menu's escape check (X1 $00:C93C, X2 $00:C622) answers
 *          "available": the escape chime, the fade, stage select.
 *   Start + Select (5 s, anywhere in a stage)  the escape's exit state. */
#include "mmx_escape.h"

#include <stdio.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "mmx_coop.h"
#include "mmx_options.h"
#include "mmx_weapon_combat.h"
#include "snes/interp_bridge.h"
#include "snes_overlay_draw.h"
#if MMX_X2_EXTENSION
#include "x2ext/mmx_x2coop.h"
#endif

enum {
  HOLD_FRAMES = 180,         /* B or Start on ESCAPE.U: 3 s */
  COMBO_UNITS = 150,         /* Start + Select: 5 s, in 2-frame units */
  UNRECORDED = 0xff,
  /* escape_hold after X / Select, until the menu has closed. */
  TAP_CAMERA = 0xfd, TAP_TELEPORT = 0xfe,         /* escape_hold, no hold running: the cursor the
                                menu opened on, or this */
  /* escape_flags */
  HELD_MASK = 3, HELD_B = 1, HELD_START = 2, HELD_COMBO = 3,
  SEAT_BIT = 4,              /* the seat holding it */
  CYCLE_SHIFT = 3, CYCLE_MASK = 7 << 3,  /* Select's next landing (0..4) */
  DONE = 0x40,               /* a command acted; the menu is closing (B: a
                                restart is due once it has) */
  PREVIOUS = 0x80,           /* X or Select held last frame (Start + Select:
                                the half of a 2-frame unit) */
  BODY_HEIGHT = 44,
};
enum { CHECK_NATIVE, CHECK_AVAILABLE };

typedef struct Game {
  uint16_t body, cursor, exit_item, mode, pause_bit, lives, camera, limits, limit_targets[4];
  uint16_t previous_camera;
} Game;
/* X1: player $0BA8, pause $1F10 = 6/8 with $C3 bit 7, camera $1E4D/$1E50
 * (previous $1E6A/$1E6C), room $1E56..$1E5C (targets $1E5E/$1E60/$1E68/
 * $1E6E). X2 keeps X1's layout $10 later, its player at $09D8. */
static const Game kX1 = {0x0ba8, 0x1ed2, 9, 0x1f10, 0xc3, 0x1f80, 0x1e4d, 0x1e56,
                         {0x1e5e, 0x1e60, 0x1e68, 0x1e6e}, 0x1e6a};
static const Game kX2 = {0x09d8, 0x1ee2, 0x1e, 0x1f21, 0xc4, 0x1fb3, 0x1e5d, 0x1e66,
                         {0x1e6e, 0x1e70, 0x1e78, 0x1e7e}, 0x1e7a};

static bool enabled;
static uint8_t check_answer;   /* this frame's forced answer (from the flags) */

void MmxEscapeSetEnabled(bool on) { enabled = on; }
bool MmxEscapeEnabled(void) { return enabled; }

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put_word(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool in_stage(const uint8_t *r, bool x2) {
  return x2 ? r[0xd0] == 2 && r[0xd1] == 4 : r[0xd1] == 2 && r[0xd2] == 4;
}
static bool paused(const uint8_t *r, const Game *g, bool x2) {
  return in_stage(r, x2) && (r[g->mode] == 6 || r[g->mode] == 8) && (r[g->pause_bit] & 0x80);
}
/* X2's EXIT is cursor $1E (after four Sub Tank places, owned or not) and
 * the menu (D = $1ED8) is in EXIT's own sub-state, $1EDA = 4. */
static bool highlighted(const uint8_t *r, const Game *g, bool x2) {
  return paused(r, g, x2) && r[g->cursor] == g->exit_item && (!x2 || r[0x1eda] == 4);
}
/* Where Start closes the menu from. X2's menu keeps its column as a
 * sub-state ($1EDA: 0 weapons, 2 Sub Tanks, 4 EXIT), which has to follow. */
static void set_cursor(uint8_t *r, const Game *g, bool x2, unsigned item) {
  r[g->cursor] = (uint8_t)item;
  if (x2) r[0x1eda] = item >= 0x1e ? 4 : item >= 0x16 ? 2 : 0;
}
/* The menu gone (its own state machine back to gameplay). */
static bool closed(const uint8_t *r, const Game *g) { return r[g->mode] < 6; }

/* ---- Camera ------------------------------------------------------------- */
/* Back on the player, within the room; a room that no longer holds the
 * player (a camera left in the wrong room) is widened to hold it. When the
 * camera is already on the player, the room opens a screen further each
 * way, for a camera wall that keeps the player out of where it should go. */
static void reset_camera(uint8_t *r, const Game *g) {
  const uint8_t *b = r + g->body;
  const int px = (int)word(b + 5), py = (int)word(b + 8);
  /* Already on the player: the room is what holds it back. */
  const int dx = (int)word(r + g->camera) - (px - 128), dy = (int)word(r + g->camera + 3) - (py - 128);
  const bool unlock = dx >= -8 && dx <= 8 && dy >= -8 && dy <= 8;
  int lim[4];
  for (unsigned i = 0; i < 4; ++i) lim[i] = (int)word(r + g->limits + i * 2);
  const int want_x = px - 128, want_y = py - 128;
  if (want_x < lim[0]) lim[0] = want_x;
  if (want_x > lim[1]) lim[1] = want_x;
  if (want_y < lim[2]) lim[2] = want_y;
  if (want_y > lim[3]) lim[3] = want_y;
  if (unlock) { lim[0] -= 256; lim[1] += 256; lim[2] -= 224; lim[3] += 224; }
  lim[0] = clamp_int(lim[0], 0, 8192 - 256); lim[1] = clamp_int(lim[1], lim[0], 8192 - 256);
  lim[2] = clamp_int(lim[2], 0, 8192 - 224); lim[3] = clamp_int(lim[3], lim[2], 8192 - 224);
  for (unsigned i = 0; i < 4; ++i) {
    put_word(r + g->limits + i * 2, (unsigned)lim[i]);
    put_word(r + g->limit_targets[i], (unsigned)lim[i]);
  }
  const int cx = clamp_int(want_x, lim[0], lim[1]), cy = clamp_int(want_y, lim[2], lim[3]);
  put_word(r + g->camera, (unsigned)cx); put_word(r + g->previous_camera, (unsigned)cx);
  put_word(r + g->camera + 3, (unsigned)cy); put_word(r + g->previous_camera + 2, (unsigned)cy);
}

/* ---- Landing ------------------------------------------------------------ */
/* The co-op teleport's ground rules (mmx_coop.c): walkable classes, a solid
 * surface exactly at `floor`, and room above it for the body. X2 keeps X1's
 * class numbers (its map comes through MmxWeaponsSetTerrainSource). */
static bool ground_type(unsigned type) {
  return type == 0x13 || (type >= 1 && type <= 12) || (type >= 0x34 && type <= 0x38) ||
         (type >= 0x3b && type <= 0x3d);
}
static bool ground_at(const uint8_t *r, int x, int floor) {
  int surface = 0;
  if (!ground_type(MmxWeaponsTerrainClass(r, x, floor)) ||
      !MmxWeaponsTerrainSolid(r, x, floor, true, &surface) || surface != floor)
    return false;
  for (int y = floor - BODY_HEIGHT; y < floor; y += 4)
    for (int dx = -10; dx <= 10; dx += 5) {
      const unsigned t = MmxWeaponsTerrainClass(r, x + dx, y);
      if ((t >= 0x33 && t != 0x39 && t != 0x3a) || MmxWeaponsTerrainSolid(r, x + dx, y, true, NULL))
        return false;
    }
  return true;
}
/* Ground in column x on screen, nearest the player's feet. */
static bool column_floor(const uint8_t *r, int x, int feet, int top, int bottom, int *out) {
  for (int k = 0; k <= 2 * (bottom - top); ++k) {
    const int floor = feet + ((k & 1) ? (k + 1) / 2 : -(k / 2));
    if (floor < top || floor >= bottom) {
      if (feet - (k + 1) / 2 < top && feet + (k + 1) / 2 >= bottom) break;
      continue;
    }
    if (ground_at(r, x, floor)) { *out = floor; return true; }
  }
  return false;
}
/* Each player's body (co-op: the seat's own, live or stored). */
extern uint8_t *MmxCoopSeatBody(uint8_t *ram, unsigned seat);
extern bool MmxCoopEnabled(void);
#if MMX_X2_EXTENSION
extern uint8_t *MmxX2CoopSeatBody(unsigned seat);
extern bool MmxX2CoopActive(void);
#endif
static uint8_t *seat_body(uint8_t *r, const Game *g, bool x2, unsigned seat) {
#if MMX_X2_EXTENSION
  if (x2 && MmxX2CoopActive()) return MmxX2CoopSeatBody(seat);
#endif
  if (!x2 && MmxCoopEnabled()) return MmxCoopSeatBody(r, seat);
  return seat ? NULL : r + g->body;
}

/* Where each player last stood on ground in this stage: the stage's own
 * path (a landing found on screen can be on top of scenery, far above it).
 * Presentation-free bookkeeping, rebuilt from the bodies every frame. */
static struct { int16_t x, y; bool valid; } path[2];
static void track_path(uint8_t *r, const Game *g, bool x2) {
  if (!in_stage(r, x2)) { path[0].valid = path[1].valid = false; return; }
  for (unsigned i = 0; i < 2; ++i) {
    const uint8_t *b = seat_body(r, g, x2, i);
    if (!b || !(b[0x27] & 127) || !(b[0x2b] & 4) || b[2] == 0x0c) continue;
    path[i].x = (int16_t)word(b + 5); path[i].y = (int16_t)word(b + 8); path[i].valid = true;
  }
}

/* Landings: left side, right side, the middle of the screen, back on the
 * stage's path (where the player last stood), the other player. */
enum { LAND_LEFT, LAND_RIGHT, LAND_MIDDLE, LAND_PATH, LAND_PARTNER, LANDINGS };
static bool landing(uint8_t *r, const Game *g, bool x2, unsigned seat, unsigned side, int *ox, int *oy) {
  if (side == LAND_PATH) {
    if (!path[seat].valid) return false;
    *ox = path[seat].x; *oy = path[seat].y;
    return true;
  }
  if (side == LAND_PARTNER) {
    const uint8_t *o = seat_body(r, g, x2, seat ^ 1);
    if (!o || !(o[0x27] & 127)) return false;
    *ox = (int)word(o + 5); *oy = (int)word(o + 8);
    return true;
  }
  const uint8_t *b = seat_body(r, g, x2, seat);
  if (!b) return false;
  const int cam_x = (int)word(r + g->camera), cam_y = (int)word(r + g->camera + 3);
  const int feet = (int)word(b + 8) + 16;
  const int top = cam_y + BODY_HEIGHT + 8, bottom = cam_y + 216;
  for (int n = 0; n <= 96; n += 4) {
    int x;
    if (side == 0) x = cam_x + 24 + n;
    else if (side == 1) x = cam_x + 232 - n;
    else x = cam_x + 128 + ((n / 4) & 1 ? -(n / 2) : n / 2);
    int floor;
    if (column_floor(r, x, clamp_int(feet, top, bottom - 1), top, bottom, &floor)) {
      *ox = x; *oy = floor - 16;
      return true;
    }
  }
  return false;
}
static unsigned teleport(uint8_t *r, const Game *g, bool x2, unsigned seat, unsigned cycle) {
  uint8_t *b = seat_body(r, g, x2, seat);
  if (!b) return LANDINGS;
  for (unsigned n = 0; n < LANDINGS; ++n) {
    const unsigned side = (cycle + n) % LANDINGS;
    int x, y;
    if (!landing(r, g, x2, seat, side, &x, &y)) continue;
    put_word(b + 5, (unsigned)x); put_word(b + 8, (unsigned)y);
    put_word(b + 0x22, (unsigned)x); put_word(b + 0x24, (unsigned)y);
    put_word(b + 0x1a, 0);  /* no fall speed carried over */
    if (b == r + g->body) reset_camera(r, g);
    return side;
  }
  return LANDINGS;
}

/* ---- Restart, leave ------------------------------------------------------ */
/* The death state's own ending, without a death: X1's $D3 = 6 ($00:9B23)
 * and X2's $D2 = 6 ($00:99AF) wait for "death done" ($1F0B / $1F1C), then
 * fade out, take a life and reload at the checkpoint (the stage's start,
 * as from stage select, when none was reached). A life is added first, so
 * none is lost; nobody's health changes. */
static void restart(uint8_t *r, const Game *g, bool x2) {
  if (r[g->lives] < 99) ++r[g->lives];
  if (x2) { r[0xd2] = 6; r[0xd3] = 0; r[0x1f1c] = 1; }
  else { r[0xd3] = 6; r[0xd4] = 0; r[0x1f0b] = 1; }
}
/* Stage select from anywhere in a stage: the escape's own exit state (X1
 * $D3 = $0A: password screen, then stage select; X2 $D2 = $0A). */
static void leave(uint8_t *r, bool x2) {
  if (x2) { r[0xd2] = 0x0a; r[0xd3] = 0; }
  else { r[0xd3] = 0x0a; r[0xd4] = 0; }
}

/* ---- Frame -------------------------------------------------------------- */
static void save(MmxOptionsState *o, unsigned hold, unsigned flags) {
  o->escape_hold = (uint8_t)(hold > 255 ? 255 : hold);
  o->escape_flags = (uint8_t)flags;
  (void)MmxOptionsSetState(o);
}
static void write_inputs(uint32_t *inputs, const uint16_t pads[2], uint16_t back, const uint16_t give[2]) {
  const uint16_t p0 = (uint16_t)((pads[0] & ~back) | give[0]);
  const uint16_t p1 = (uint16_t)((pads[1] & ~back) | give[1]);
  *inputs = (*inputs & ~0x00ffffffu) | p0 | ((uint32_t)p1 << 12);
}

void MmxEscapeFrameBegin(uint32_t *inputs, bool x2) {
  check_answer = CHECK_NATIVE;
  if (!enabled || !inputs) return;
  MmxOptionsState o = MmxOptionsGetState();
  unsigned hold = o.escape_hold, flags = o.escape_flags;
  const Game *g = x2 ? &kX2 : &kX1;
  uint8_t *r = g_ram;
  unsigned held = flags & HELD_MASK, seat = (flags & SEAT_BIT) ? 1 : 0;
  const uint16_t pads[2] = {(uint16_t)(*inputs & 0x0fff), (uint16_t)((*inputs >> 12) & 0x0fff)};
  uint16_t give[2] = {0, 0};
  const uint16_t mine = SNES_PAD_B | SNES_PAD_START | SNES_PAD_SELECT | SNES_PAD_X;
  const uint16_t combo = SNES_PAD_START | SNES_PAD_SELECT;
  const bool stage = in_stage(r, x2), menu = paused(r, g, x2);
  if (!menu) track_path(r, g, x2);

  /* Start + Select, 5 s: stage select from anywhere in a stage. */
  int combo_seat = -1;
  for (unsigned i = 0; i < 2 && combo_seat < 0; ++i)
    if ((pads[i] & combo) == combo) combo_seat = (int)i;
  if (held == HELD_COMBO) {
    if (combo_seat < 0 || !stage) { held = 0; flags &= ~(unsigned)(HELD_MASK | SEAT_BIT | DONE | PREVIOUS); hold = UNRECORDED; }
    else if (!(flags & DONE)) {
      if (flags & PREVIOUS) {
        flags &= ~(unsigned)PREVIOUS;
        if (++hold >= COMBO_UNITS) { leave(r, x2); flags |= DONE; }
      } else flags |= PREVIOUS;
    }
    if (held) {
      /* Start and Select stay the command's (no pause, no CHANGE PLAYER). */
      write_inputs(inputs, pads, menu ? mine : combo, give);
      save(&o, hold, flags);
      return;
    }
  } else if (stage && combo_seat >= 0 && !(flags & DONE)) {
    seat = (unsigned)combo_seat;
    flags = (flags & ~(unsigned)(HELD_MASK | SEAT_BIT | PREVIOUS)) | HELD_COMBO | (seat ? SEAT_BIT : 0);
    write_inputs(inputs, pads, menu ? mine : combo, give);
    save(&o, 0, flags);
    return;
  }

  if (!menu) {
    /* A held B's restart, once the menu is gone. */
    if (held == HELD_B && (flags & DONE)) {
      if (!stage) { flags &= ~(unsigned)(HELD_MASK | SEAT_BIT | DONE); }
      else if (closed(r, g)) { restart(r, g, x2); flags &= ~(unsigned)(HELD_MASK | SEAT_BIT | DONE); }
    } else if (!held && (flags & DONE) && (hold == TAP_CAMERA || hold == TAP_TELEPORT)) {
      /* X / Select: now the menu is gone. */
      if (!stage) flags &= ~(unsigned)(SEAT_BIT | DONE);
      else if (closed(r, g)) {
        const unsigned seat = (flags & SEAT_BIT) ? 1 : 0;
        if (hold == TAP_CAMERA) reset_camera(r, g);
        else {
          /* The player who pressed it moves. */
          const unsigned cycle = ((flags & CYCLE_MASK) >> CYCLE_SHIFT) % LANDINGS;
          const unsigned side = teleport(r, g, x2, seat, cycle);
          flags = (flags & ~(unsigned)CYCLE_MASK) |
              (((side < LANDINGS ? side : cycle) + 1) % LANDINGS) << CYCLE_SHIFT;
        }
        flags &= ~(unsigned)(SEAT_BIT | DONE);
        hold = UNRECORDED;
      }
    } else flags &= ~(unsigned)(HELD_MASK | SEAT_BIT | DONE);
    flags &= ~(unsigned)PREVIOUS;
    if (!stage) flags &= ~(unsigned)CYCLE_MASK;
    save(&o, (flags & DONE) ? hold : UNRECORDED, flags);
    return;
  }
  /* The menu closing after a command: its answer stands. */
  if (flags & DONE) {
    if (held == HELD_START) check_answer = CHECK_AVAILABLE;
    write_inputs(inputs, pads, mine, give);
    save(&o, hold, flags);
    return;
  }
  /* The cursor the menu opened on: the weapon in use. */
  if (!held && hold == UNRECORDED) hold = r[g->cursor];
  if (!highlighted(r, g, x2)) {
    /* Off ESCAPE.U: a hold in progress ends. */
    if (held) { held = 0; flags &= ~(unsigned)(HELD_MASK | SEAT_BIT); hold = UNRECORDED; }
    save(&o, hold, flags & ~(unsigned)PREVIOUS);
    return;
  }
  const unsigned back_to = hold < 0x40 && !held ? hold : 0;
  /* X and Select: act, then close the menu from the weapon in use. */
  const bool fresh = !(flags & PREVIOUS) && !held;
  int tap_seat = -1;
  /* Player 2 first: a menu player 2 opened gets its buttons in player 1's
   * place too (X2 co-op's menu owner). */
  for (int i = 1; i >= 0 && tap_seat < 0; --i)
    if (fresh && (pads[i] & (SNES_PAD_X | SNES_PAD_SELECT))) tap_seat = i;
  if (tap_seat >= 0) {
    /* It acts once the menu has closed (the menu keeps the live camera
     * aside while it is up): what to do waits in escape_hold, the player who
     * pressed it in the seat bit. */
    hold = (pads[tap_seat] & SNES_PAD_X) ? TAP_CAMERA : TAP_TELEPORT;
    flags = (flags & ~(unsigned)SEAT_BIT) | (tap_seat ? SEAT_BIT : 0);
    set_cursor(r, g, x2, back_to);
    /* Start closes it: also in player 1's place, which X2's menu reads. */
    give[tap_seat] = give[0] = SNES_PAD_START;
    flags |= DONE;
  }
  const uint16_t both = (uint16_t)(pads[0] | pads[1]);
  flags = (both & (SNES_PAD_X | SNES_PAD_SELECT)) ? flags | PREVIOUS : flags & ~(unsigned)PREVIOUS;
  /* B and Start: a short press is the game's own; held 3 s, they act. */
  if (tap_seat >= 0) {
  } else if (held) {
    const uint16_t button = held == HELD_B ? SNES_PAD_B : SNES_PAD_START;
    if (!(pads[seat] & button)) {
      give[seat] = button;  /* one frame, on release */
      flags &= ~(unsigned)(HELD_MASK | SEAT_BIT);
      hold = UNRECORDED;
    } else if (++hold >= HOLD_FRAMES) {
      flags |= DONE;
      give[seat] = SNES_PAD_START;
      if (held == HELD_START) check_answer = CHECK_AVAILABLE;
      /* B: the menu closes from the buster (Start on ESCAPE.U would leave a
       * beaten stage); the restart follows once it has closed. */
      else set_cursor(r, g, x2, 0);
    }
  } else {
    for (unsigned i = 0; i < 2 && !held; ++i) {
      if (pads[i] & SNES_PAD_B) { held = HELD_B; seat = i; }
      else if (pads[i] & SNES_PAD_START) { held = HELD_START; seat = i; }
    }
    if (held) { flags = (flags & ~(unsigned)(HELD_MASK | SEAT_BIT)) | held | (seat ? SEAT_BIT : 0); hold = 0; }
  }
  write_inputs(inputs, pads, mine, give);
  save(&o, hold, flags);
}

bool MmxEscapeForceExit(void) { return enabled && check_answer == CHECK_AVAILABLE; }

/* ---- The escape check --------------------------------------------------- */
/* Interpreter twin of the generated-code hooks: answer from the RTS (X1
 * $C94E, X2 $C641), A = $40 for available, 0 for not. */
static void check_hook(CpuState *cpu, uint32_t pc) {
  const bool x2 = (pc & 0xffff) == 0xc622;
  if (!MmxEscapeForceExit()) return;
  const uint8_t a = 0x40;
  cpu->A = (uint16_t)((cpu->A & 0xff00) | a);
  cpu->_flag_Z = a == 0; cpu->_flag_N = 0;
  cpu->P = (uint8_t)((cpu->P & ~0x82u) | (a ? 0 : 0x02));
  interp_bridge_pre_opcode_redirect((pc & 0xff0000) | (x2 ? 0xc641 : 0xc94e));
}
/* For the generated code: 0 native, 1 available. */
int MmxEscapeCheckAnswer(void) {
  return MmxEscapeForceExit() ? 1 : 0;
}

void MmxEscapeRegisterHooks(void) {
  static bool done;
  if (done) return;
  done = true;
  (void)interp_bridge_add_pre_opcode_hook(0x00c93c, check_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x80c93c, check_hook);
#if MMX_X2_EXTENSION
  interp_bridge_set_pre_opcode_hook_owner(1);
  (void)interp_bridge_add_pre_opcode_hook(0x00c622, check_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x80c622, check_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
#endif
}

/* ---- The command list ------------------------------------------------- */
static bool combo_shown(void) {
  const MmxOptionsState o = MmxOptionsGetState();
  return (o.escape_flags & HELD_MASK) == HELD_COMBO && !(o.escape_flags & DONE) && o.escape_hold >= 8;
}
bool MmxEscapeX1ListShown(const uint8_t *ram) {
  return enabled && ram && (highlighted(ram, &kX1, false) || (in_stage(ram, false) && combo_shown()));
}

enum { BAR_COLOR = 0x0040d0f8 };
static void fill(uint32_t *field, int stride, int x0, int y0, int w, int h, uint32_t c) {
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x) field[(ptrdiff_t)y * stride + x] = c;
}
static void text(uint32_t *field, int stride, int x, int y, const char *t, uint32_t c) {
  snes_ovl_draw_text(field, stride, 224, x + 1, y + 1, t, 0, 1);
  snes_ovl_draw_text(field, stride, 224, x, y, t, c, 1);
}
static void panel(uint32_t *field, int stride, int x0, int y0, int w, int h) {
  fill(field, stride, x0, y0, w, h, 0x00f8d040);
  fill(field, stride, x0 + 1, y0 + 1, w - 2, h - 2, 0x00101828);
}

/* Start + Select's 5 s, over the stage. */
static void draw_combo(uint32_t *field, int stride) {
  enum { X = 44, Y = 8, W = 168, H = 26 };
  const MmxOptionsState o = MmxOptionsGetState();
  panel(field, stride, X, Y, W, H);
  text(field, stride, X + (W - 12 * 8) / 2, Y + 5, "STAGE SELECT", 0x00f8d040);
  const int filled = o.escape_hold * (W - 16) / COMBO_UNITS;
  fill(field, stride, X + 8, Y + 17, filled < W - 16 ? filled : W - 16, 3, BAR_COLOR);
}

static void draw_list(uint32_t *field, int stride) {
  enum { X = 12, Y = 138, W = 232, H = 80 };
  const MmxOptionsState o = MmxOptionsGetState();
  /* A command acted: the menu is closing. */
  if (o.escape_flags & DONE) return;
  panel(field, stride, X, Y, W, H);
  char line[40];
  const unsigned cycle = ((o.escape_flags & CYCLE_MASK) >> CYCLE_SHIFT) % LANDINGS;
  static const char *const kSide[LANDINGS] = {"LEFT", "RIGHT", "MIDDLE", "TO PATH", "TO PARTNER"};
  snprintf(line, sizeof(line), "MOVE %s", kSide[cycle]);
  const char *const rows[4][2] = {
    {"X", "RESET CAMERA"},
    {"SELECT", line},
    {"HOLD B", "LAST CHECKPOINT"},
    {"HOLD START", "STAGE SELECT"},
  };
  text(field, stride, X + 6, Y + 5, "ESCAPE BUGS", 0x00f8d040);
  for (unsigned i = 0; i < 4; ++i) {
    const int y = Y + 18 + (int)i * 12;
    text(field, stride, X + 6, y, rows[i][0], 0x00f8d040);
    text(field, stride, X + 94, y, rows[i][1], 0x00f8f8f8);
  }
  text(field, stride, X + 6, Y + 67, "STUCK? HOLD START+SELECT 5S", 0x00a0a8b8);
  /* The hold's progress, under its row. */
  const unsigned held = o.escape_flags & HELD_MASK;
  if (held == HELD_B || held == HELD_START) {
    const int row = held == HELD_B ? 2 : 3;
    const int filled = (int)(o.escape_hold * 210 / HOLD_FRAMES);
    fill(field, stride, X + 11, Y + 18 + row * 12 + 9, filled < 210 ? filled : 210, 2, BAR_COLOR);
  }
}

static void draw(uint32_t *field, int stride, const uint8_t *r, const Game *g, bool x2) {
  if (combo_shown() && in_stage(r, x2)) draw_combo(field, stride);
  else if (highlighted(r, g, x2)) draw_list(field, stride);
}

void MmxEscapeDrawX1(uint32_t *pixels, int stride, int left) {
  if (!pixels || !MmxEscapeX1ListShown(g_ram)) return;
  draw(pixels + left, stride, g_ram, &kX1, false);
}

void MmxEscapeDrawX2(uint32_t *top_row, int stride, const uint8_t *brightness) {
  (void)brightness;
  if (!enabled || !top_row) return;
  draw(top_row, stride, g_ram, &kX2, true);
}
