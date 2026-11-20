/* Local co-op on the X2 page (X-tensions: X2 game extension).
 *
 * X1's co-op (mmx_coop.c) runs the player routine once per seat with that
 * seat's body projected into the player struct. X2's player routine is X1's
 * ($88:9428 = X1 $81:812E, same prologue and epilogue), so the same is done
 * here in X2's terms:
 *
 *  - Player 2's pass: just before the routine's PLD/PLP/RTL ($08:94C3) the
 *    prologue's PHD/PHP is undone, player 2's body, helpers and Zero state
 *    are selected, and the routine starts again at $08:9428. Its second end
 *    puts player 1 back and returns once.
 *  - Player 2's buttons go through X2's own action mapping ($00:E52E, X1's
 *    $00:E543 byte for byte, masks at $7E:FFC0) into the projected body.
 *  - Enemy contact ($08:D752, X1 $84:9B03) runs again with player 2's body
 *    when it found no hit on player 1, as X1's contact passes do. It is never
 *    compiled, so pre-opcode hooks see its entry and every RTL.
 *  - Player shots stay shared: both players' shots are live objects, so
 *    enemies are hit by either player natively.
 *
 * The main seat is the one in X2's player struct outside the passes: player
 * 1, or player 2 while player 1 is down. Unified: the camera shows both
 * (it reads their midpoint, $00:DE9E / $00:DEBD, as X1's co-op does) and the
 * two stay within one screen of each other. A seat's death plays out
 * without ending the stage while the other seat lives ($08:A71C's INC $1F1C
 * is undone; a main seat that falls hands the main role over first); the
 * fallen seat comes back beside the other with Select, for an extra life.
 * Objects that test the player ($08:D865, X1's $84:9C0E: item drops, solid
 * blocks, spotlights, event boxes, boss doors) test player 1 and, on a miss,
 * player 2, whose body then stays in the player struct until that object's
 * update ends (the object loops' returns), so a pickup heals player 2, a
 * block pushes player 2 out or carries it, an alarm sees player 2. A boss
 * door player 2 touches makes player 2 the main seat; either player's door
 * sends the other out, to beam in beside it on the far side, as X1 does.
 * Below the room's lowest camera ($00:E0EA, X1 $00:E12D) the other seat
 * falls as in a pit; the shared camera never kills the main seat there.
 * Shots fired in the second pass are counted on that seat's body ($0A0D),
 * and X2's own L/R weapon change never runs for Zero's seat (his L/R are
 * his own) so X's palette stays X's. */
#include "mmx_x2coop.h"
int X2WsHudLeftShift(void); /* x2_runtime.c: widescreen HUD shift */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "mmx_coop.h"
#include "mmx_coop_view.h"
#include "mmx_netplay.h"
#if SNESRECOMP_NET
#include "netplay/snes_netplay.h"
#endif
#include "mmx_renderer.h"
#include "mmx_zero.h"
#include "mmx_weapon_combat.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"

extern uint8_t g_ram[0x20000];
extern Ppu *g_ppu;

void MmxX2ZeroSetPlayerSource(uint8_t *body);

enum {
  /* The whole player struct: $09D8..$0AE7 (X1's $90 bytes plus X2's $80). */
  PLAYER = 0x09d8, BODY = 0x110,
  /* The player's three helper objects ($0AE8/$0B08/$0B28; X1 $0C38..). */
  HELPERS = 0x0ae8, HELPERS_SIZE = 0x60,
  ACTION_DEATH = 0x0c,
  CONTACT_ENTRY = 0x08d752,
  RESPAWN_FRAMES = 180, AWAY_FRAMES = 90,
  /* Player shots: nine 64-byte slots from $10D8. */
  SHOTS = 0x10d8, SHOT_SLOTS = 9, SHOT_SIZE = 0x40,
  /* Unified: how far apart the two may be (one 4:3 screen less margins). */
  APART = 224,
  PAD_SELECT = 1u << 2, PAD_START = 1u << 3, PAD_L = 1u << 10, PAD_R = 1u << 11,
  /* Side by side: how far player 2's camera may be from player 1's. */
  LEASH_X = 128, LEASH_Y = 64,
};
enum { SEAT_ABSENT, SEAT_ALIVE, SEAT_FALLEN };

typedef struct Seat Seat;
struct Seat {
  uint8_t body[BODY];
  uint8_t helpers[HELPERS_SIZE];
  MmxZeroState zero;
  uint16_t pad, pressed;
  uint16_t actions;          /* last frame's mapped actions (+$36) */
  uint8_t character, status;
  uint16_t wait, away;
  uint8_t beam_next;         /* its next arrival beams in (not a stage's start); fallen: Select asked for a revive */
};

typedef struct X2CoopState {
  bool configured, active;
  Seat seats[2];
  /* Seat whose body is live at $09D8; seat whose Zero state is selected;
   * the main seat (live outside the passes). */
  uint8_t live, zero_owner, main;
  /* Shot slots the other seat fired (bit n: slot n), and the slots in use
   * as its pass began. */
  uint16_t other_shots, shots_before;
  /* The main seat's shot count ($0A0D) and the slots in use as its pass
   * ended: the shots that ended by the next main pass took their decrements
   * from it, whoever fired them. */
  uint16_t mark_slots;
  uint8_t count_mark, mark_valid;
  uint8_t joypad_nmi[6];     /* X2's own $A8..$AD for this frame */
  /* Player 2's pass: 0 none, 2 running. Registers after player 1's pass. */
  uint8_t pass;
  uint16_t a, x, y;
  uint8_t db, p;
  uint8_t restart, restart_f, restart_g; /* $1F1C/$1F1F/$1F20 before pass 2 */
  /* Enemy contact passes. */
  uint8_t contact, terrain;
  uint8_t joypad[6]; /* player 1's $A8..$AD during player 2's pass */
  /* The palette buffer $0300..$04FF before player 2's pass: the death
   * sequence's fade to white ($08:A6FF JSR $C253) is the stage's end. */
  uint8_t palette[0x200];
  uint16_t contact_s, contact_d, contact_a, contact_x, contact_y;
  uint8_t contact_db, contact_p, contact_entry_p;
  /* Side by side: player 2's camera. */
  int cam2_x, cam2_y;
  bool cam2_valid;
  /* The pause menu's owner (the seat whose Start opened it) and last
   * frame's Start per seat. */
  uint8_t menu_owner, start_prev;
  /* The touch test's second seat ($08:D865): 0 none, 1 player 1's test, 2
   * player 2's, 3 player 2 kept in for the rest of object ov_d. */
  uint8_t ov, ov_p, ov_db;
  uint16_t ov_s, ov_d, ov_a, ov_x, ov_y;
  uint32_t ov_return;        /* the JSL's pushed return address */
} X2CoopState;
static X2CoopState s;

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put_word(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static unsigned other(void) { return s.main ^ 1u; }
static bool shared_screen(void);
static void hand_main(unsigned seat);
void MmxX2CoopObjectDone(void);

static uint16_t shot_slots(void) {
  uint16_t used = 0;
  for (unsigned i = 0; i < SHOT_SLOTS; ++i)
    if (g_ram[SHOTS + i * SHOT_SIZE]) used |= (uint16_t)(1u << i);
  return used;
}

static bool debug_on(void) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("MMX_X2COOP_DEBUG"); on = e && *e && *e != '0'; }
  return on != 0;
}

static bool gameplay(void) { return g_ram[0xd0] == 2 && g_ram[0xd1] == 4; }

bool MmxX2CoopActive(void) { return s.active; }

/* ── Co-op physics diagnostics (developer mod) on the X2 page ──
 * X1's rows (mmx_coop.c) are in X1's terms; these are X2's: both seats at
 * every frame end and the co-op events, in logs/coop-x2physics-*.csv, and
 * one row per simulated frame in logs/coop-x2netplay-*.csv during netplay
 * (compare the hashes between both players' files for the first tick they
 * disagree). Observation only. */
extern int snes_frame_counter;
static const char kX2PhysicsHeader[] =
    "sequence,host_frame,event,stage,d0,d1,d2,d3,live,pass,contact,terrain,camera_x,camera_y,"
    "camera2_x,camera2_y,freeze_flags,seat,character,status,wait,away,pad,x,y,previous_x,"
    "previous_y,vx,vy,hp,max_hp,ground,action,substate,body\n";
static const char kX2NetplayHeader[] =
    "sequence,host_frame,sim_tick,rollback,slot,host,input_delay,p1_pad,p2_pad,live,stage,"
    "d0,d1,d2,d3,wram_hash,coop_hash,p1_hash,p2_hash\n";

static uint8_t *seat_body(unsigned seat);
static void settle_shots(void);
static void hand_main(unsigned seat);
static void apply_input(uint8_t *body, Seat *seat, uint16_t input);
static void seat_joypad(const Seat *seat);

static void hex(char *out, const uint8_t *p, size_t n) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) { out[i * 2] = digits[p[i] >> 4]; out[i * 2 + 1] = digits[p[i] & 15]; }
  out[n * 2] = 0;
}

static uint32_t fnv(const void *data, size_t size) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
  return h;
}

static void diag_rows(const char *event, int only_seat) {
  if (!MmxCoopDiagnosticsEnabled() || !s.active) return;
  const unsigned sequence = MmxCoopDiagnosticSequence();
  char flags[15], body[BODY * 2 + 1];
  hex(flags, g_ram + 0x1f25, 7);
  for (unsigned seat = 0; seat < 2; ++seat) {
    if (only_seat >= 0 && (unsigned)only_seat != seat) continue;
    const Seat *p = &s.seats[seat];
    const uint8_t *b = seat_body(seat);
    hex(body, b, BODY);
    char row[1024 + sizeof(body)];
    const int n = snprintf(row, sizeof(row),
        "%u,%d,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%s,%u,%s,%u,%u,%u,%04x,%u,%u,%u,%u,"
        "%d,%d,%u,%u,%u,%02x,%02x,%s\n",
        sequence, snes_frame_counter, event, g_ram[0x1fbf], g_ram[0xd0], g_ram[0xd1],
        g_ram[0xd2], g_ram[0xd3], s.live, s.pass, s.contact, s.terrain,
        word(g_ram + 0x1e5d), word(g_ram + 0x1e60), s.cam2_valid ? s.cam2_x : -1,
        s.cam2_valid ? s.cam2_y : -1, flags, seat + 1,
        p->character == MMX_COOP_ZERO ? "zero" : "x",
        p->status, p->wait, p->away, p->pad, word(b + 5), word(b + 8), word(b + 0x22),
        word(b + 0x24), (int16_t)word(b + 0x1a), (int16_t)word(b + 0x1c), b[0x27] & 127,
        g_ram[0x1fd1], b[0x2b], b[2], b[3], body);
    if (n < 0 || (size_t)n >= sizeof(row)) return;
    if (!MmxCoopDiagnosticWrite("x2physics", kX2PhysicsHeader, row, (size_t)n)) return;
  }
}

static void diag_event(const char *event, unsigned seat) { diag_rows(event, (int)seat); }

void MmxX2CoopDiagnosticFrame(void) {
  if (!MmxCoopDiagnosticsEnabled() || !s.active) return;
  diag_rows("frame-end", -1);
#if SNESRECOMP_NET
  if (!MmxNetplayActive()) return;
  char row[512];
  const int n = snprintf(row, sizeof(row),
      "%u,%d,%u,%d,%d,%d,%d,%04x,%04x,%u,%u,%u,%u,%u,%u,%08x,%08x,%08x,%08x\n",
      MmxCoopDiagnosticSequence(), snes_frame_counter, (unsigned)snes_netplay_sim_tick(),
      snes_netplay_rollback_active(), snes_netplay_local_slot(), snes_netplay_is_host(),
      snes_netplay_input_delay(), s.seats[0].pad, s.seats[1].pad, s.live, g_ram[0x1fbf],
      g_ram[0xd0], g_ram[0xd1], g_ram[0xd2], g_ram[0xd3], fnv(g_ram, 0x20000),
      fnv(s.seats, sizeof(s.seats)), fnv(seat_body(0), BODY), fnv(seat_body(1), BODY));
  if (n > 0 && (size_t)n < sizeof(row))
    (void)MmxCoopDiagnosticWrite("x2netplay", kX2NetplayHeader, row, (size_t)n);
#endif
}

/* The seat that plays Zero, or the live one when neither does. */
static unsigned zero_seat(void) {
  for (unsigned i = 0; i < 2; ++i)
    if (s.seats[i].character == MMX_COOP_ZERO) return i;
  return s.live;
}

static uint8_t *seat_body(unsigned seat) {
  return seat == s.live ? g_ram + PLAYER : s.seats[seat].body;
}

static void zero_select(unsigned seat) {
  MmxCoopSetSeatOverride(true, seat, s.seats[seat].pad, s.seats[seat].pressed);
  if (seat == s.zero_owner) return;
  s.seats[s.zero_owner].zero = MmxZeroGetState();
  MmxZeroSelectState(s.seats[seat].zero);
  s.zero_owner = (uint8_t)seat;
}

static void body_select(unsigned seat, bool helpers) {
  if (seat == s.live) return;
  Seat *out = &s.seats[s.live], *in = &s.seats[seat];
  memcpy(out->body, g_ram + PLAYER, BODY);
  if (helpers) {
    memcpy(out->helpers, g_ram + HELPERS, HELPERS_SIZE);
    memcpy(g_ram + HELPERS, in->helpers, HELPERS_SIZE);
  }
  memcpy(g_ram + PLAYER, in->body, BODY);
  s.live = (uint8_t)seat;
}

/* Outside the passes: player 1 live, Zero's seat owns Zero's state, and the
 * Zero bridge's view shows Zero's seat. */
static void settle(void) {
  if (s.live != s.main) {
    /* (Callers put the main seat back first.) */
  }
  const unsigned z = zero_seat();
  zero_select(z);
  MmxX2ZeroSetPlayerSource(z == s.live ? NULL : s.seats[z].body);
}

/* The world's freeze flags ($1F25..$1F2B): a player's death sets them (the
 * objects stop, a pillar with them, and whoever stands on it falls). Only
 * the last player's death freezes the world: the flags as a pass found them
 * are put back after a death that leaves the other playing. Host state: a
 * pass never spans a frame. */
enum { FREEZE = 0x1f25, FREEZE_SIZE = 7 };
static uint8_t freeze_main[FREEZE_SIZE], freeze_other[FREEZE_SIZE];

void MmxX2CoopPassBegin(void) {
  if (!s.active) return;
  if (s.pass == 0 && s.live == s.main) memcpy(freeze_main, g_ram + FREEZE, FREEZE_SIZE);
  if (s.pass == 0) MmxX2CoopObjectDone();
  if (s.pass == 0 && s.live == s.main) settle_shots();
  /* The main pass is player 2's while player 1 is down: X2 read port 1 for
   * it, so player 2's buttons replace them. */
  if (s.pass == 0 && s.live == s.main && s.main != 0) {
    memcpy(s.joypad_nmi, g_ram + 0xa8, sizeof(s.joypad_nmi));
    apply_input(g_ram + PLAYER, &s.seats[s.main], s.seats[s.main].pad);
    seat_joypad(&s.seats[s.main]);
  }
  zero_select(s.live);
  MmxX2ZeroSetPlayerSource(NULL);
}

bool MmxX2CoopLiveOwnsZero(void) { return !s.active || s.zero_owner == s.live; }
/* X2 pauses on port 1's Start and its menu reads port 1 and shows the main
 * seat ($09D8: its weapons, its health, Zero's page for Zero). Player 2's
 * Start opens it too, as player 2's: player 2 is the main seat from the
 * pause's start ($1F21 set) until it has closed, and player 2's buttons
 * drive it (X1 co-op's menu owner); player 1 is
 * the main seat again once it has closed ($1F21 back to 0), or when it never
 * opened (an event refused the pause). start_prev: bits 0-1 last frame's
 * Start per seat, bit 2 the menu seen open, bits 3-7 frames waited for it.
 * Runs on the frame's inputs (seat 0 bits 0-11, seat 1 bits 12-23) before
 * the other filters, outside the passes. */
void MmxX2CoopMenuInputs(uint32_t *inputs) {
  enum { START = 1u << 3, SEEN = 4, WAIT_STEP = 8, WAIT_MAX = 30 };
  if (!s.active || !inputs) return;
  uint32_t w = *inputs;
  uint16_t p0 = (uint16_t)(w & 0x0fff);
  const uint16_t p1 = (uint16_t)((w >> 12) & 0x0fff);
  const uint8_t now = (uint8_t)((p0 & START ? 1 : 0) | (p1 & START ? 2 : 0));
  const uint8_t pressed = (uint8_t)(now & ~s.start_prev & 3);
  unsigned seen = s.start_prev & SEEN, wait = s.start_prev / WAIT_STEP;
  const bool menu = (g_ram[0x1f21] == 6 || g_ram[0x1f21] == 8) && (g_ram[0xc4] & 0x80);
  if (s.menu_owner == 1) {
    if (g_ram[0x1f21]) {
      /* The pause has begun (port 1's Start): its menu is player 2's. */
      seen = SEEN;
      if (s.main == 0 && s.live == s.main) hand_main(1);
    } else if (seen || ++wait > WAIT_MAX) {
      /* Closed (or never opened): player 1 the main seat again. */
      s.menu_owner = 0;
      seen = 0; wait = 0;
      if (s.main == 1 && s.live == s.main && s.seats[0].status == SEAT_ALIVE &&
          (s.seats[0].body[0x27] & 127)) {
        hand_main(0);
        /* The close's fade ends in $00:CB7E, which equips the menu's choice
         * ($1EE2) on $09D8, player 1 by then: its own weapon instead. */
        g_ram[0x1ee2] = g_ram[PLAYER + 0x33];
      }
    }
  }
  if (s.menu_owner != 1) { seen = 0; wait = 0; }
  s.start_prev = (uint8_t)(now | seen | (wait > 31 ? 31 : wait) * WAIT_STEP);
  if (s.menu_owner != 1 && s.main == 1 && s.seats[1].status == SEAT_ALIVE) {
    /* Player 2 the main seat (player 1 down): its Start is port 1's too
     * (only the passes read player 2's buttons) and its buttons drive the
     * menu. */
    s.menu_owner = 0;
    if (menu) p0 = p1;
    else if ((pressed & 2) && gameplay() && g_ram[0xd2] == 4) p0 |= START;
    *inputs = (w & ~0x0fffu) | p0;
    return;
  }
  if (s.menu_owner != 1 && (s.seats[1].status == SEAT_ABSENT || s.main != 0)) {
    s.menu_owner = 0;
    *inputs = (w & ~0x0fffu) | p0;
    return;
  }
  if (!menu && s.menu_owner != 1) {
    if (pressed & 1) s.menu_owner = 0;
    else if ((pressed & 2) && gameplay() && g_ram[0xd2] == 4 && s.live == s.main &&
             s.seats[1].status == SEAT_ALIVE && (s.seats[1].body[0x27] & 127) &&
             s.seats[1].body[2] != ACTION_DEATH) {
      s.menu_owner = 1;
      p0 |= START;
    }
  } else if (s.menu_owner == 1) {
    p0 = p1;
  }
  *inputs = (w & ~0x0fffu) | p0;
}

/* ---- Objects testing the player ($08:D865) ------------------------------ */
enum { OVERLAP_ENTRY = 0x08d865 };
static const uint32_t kOverlapReturns[] = {0x08d86c, 0x08d874, 0x08d95d};
/* Boss doors ($01:DCD1 / $01:DFD6) call it from $01:DCE0 / $01:DFE5. */
static bool door_caller(uint32_t ret) {
  const uint32_t r = ret & 0x7fffff;
  return r == 0x01dce3 || r == 0x01dfe8;
}
/* Self-contained routines objects call for the player: $08:CC7F, the solid
 * object (Wire Sponge's pillars, blocks: the touch test, pushing the player
 * out, landing on its top with the object's .2C bit 0 and the player's
 * $0A04, crushing), and $08:D651 (touching: the player's $0A04 bits 7/6,
 * carried along). Each runs once per seat, from the same entry registers and
 * .2C, and the two .2C answers are OR-ed, so both players can stand on one
 * pillar (the touch test's fallback gives player 2 a turn only when player 1
 * misses); the caller then reads player 1's scratch ($0000..). Host state:
 * the pass never spans a frame. */
/* Also $07:E22A (JSR, RTS at $E264), Flame Stag's rising lava: a player
 * below its surface is pushed up and burned (8, or $7F deep under it). */
static const uint32_t kSolidEntries[] = {0x08cc7f, 0x08d651, 0x07e22a};
static const uint32_t kSolidReturns[] = {0x08cc9f, 0x08cd2d, 0x08d672, 0x07e264};
static struct {
  uint8_t pass, entry_2c, first_2c, p, db, rp, rdb, other_04;
  uint16_t s, d, a, x, y, ra, rx, ry;
  uint32_t entry;
  uint8_t scratch[0x20]; /* $0000..$001F after player 1's turn (the touch test's distances) */
} solid;
/* Who landed on each solid object's top this frame ($08:CC7F's .2C bit 0,
 * per seat): $08:CD2E, the object's rider carry (its movement added to the
 * player's position, called while .2C bit 0 is set), moves those seats and
 * no other. */
enum { SOLID_CC7F = 0x08cc7f, CARRY_ENTRY = 0x08cd2e, CARRY_RTL = 0x08cd4c, CARRY_SLOTS = 16 };
static struct { uint16_t d; uint8_t riders; } carry[CARRY_SLOTS];
static unsigned carry_count;
static int carry_frame = -1;
static void carry_note(uint16_t d, uint8_t riders) {
  if (carry_frame != snes_frame_counter) { carry_frame = snes_frame_counter; carry_count = 0; }
  for (unsigned i = 0; i < carry_count; ++i)
    if (carry[i].d == d) { carry[i].riders = riders; return; }
  if (carry_count < CARRY_SLOTS) { carry[carry_count].d = d; carry[carry_count].riders = riders; ++carry_count; }
}
static int carry_riders(uint16_t d) {
  if (carry_frame != snes_frame_counter) return -1;
  for (unsigned i = 0; i < carry_count; ++i)
    if (carry[i].d == d) return carry[i].riders;
  return -1;
}
int MmxX2CoopCarryInterp(void) { return s.active && carry_frame == snes_frame_counter && carry_count; }

static bool solid_entry(uint32_t pc) {
  for (unsigned i = 0; i < sizeof(kSolidEntries) / sizeof(kSolidEntries[0]); ++i)
    if ((pc & 0x7fffff) == kSolidEntries[i]) return true;
  return false;
}
static bool overlap_ready(void) {
  const Seat *o = &s.seats[other()];
  return s.active && !s.pass && !s.contact && !s.terrain && !solid.pass && s.live == s.main &&
      gameplay() && g_ram[0xd2] == 4 && o->status == SEAT_ALIVE && (o->body[0x27] & 127) &&
      o->body[2] != ACTION_DEATH;
}
static void carry_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (!s.active || s.pass || s.live != s.main) return;
  const int riders = carry_riders(cpu->D);
  if (riders < 0) return;
  const uint8_t *o = g_ram + cpu->D;
  const int dx = (int)word(o + 5) - (int)word(o + 0x22);
  const int dy = (int)word(o + 8) - (int)word(o + 0x24);
  if (riders & (1u << other())) {
    uint8_t *b = s.seats[other()].body;
    put_word(b + 5, (unsigned)((int)word(b + 5) + dx));
    put_word(b + 8, (unsigned)((int)word(b + 8) + dy));
  }
  /* Only the seats on its top: the live one carried as natively, or not. */
  if (!(riders & (1u << s.main))) interp_bridge_pre_opcode_redirect(CARRY_RTL);
}

int MmxX2CoopSolidInterp(void) { return s.ov == 3 || overlap_ready(); }

static void solid_hook(CpuState *cpu, uint32_t pc) {
  if (solid_entry(pc)) {
    if (s.ov == 3 && cpu->D != s.ov_d) MmxX2CoopObjectDone();
    if (s.ov || !overlap_ready()) return;
    cpu_mirrors_to_p(cpu);
    solid.pass = 1;
    solid.entry = pc & 0x7fffff;
    solid.s = cpu->S; solid.d = cpu->D;
    solid.a = cpu->A; solid.x = cpu->X; solid.y = cpu->Y; solid.p = cpu->P; solid.db = cpu->DB;
    solid.entry_2c = g_ram[(uint16_t)(cpu->D + 0x2c)];
    return;
  }
  if (!solid.pass || cpu->S != solid.s || cpu->D != solid.d) return;
  uint8_t *top = g_ram + (uint16_t)(solid.d + 0x2c);
  if (solid.pass == 1) {
    /* Player 1's answer kept; the same call for player 2. */
    solid.first_2c = *top;
    memcpy(solid.scratch, g_ram, sizeof(solid.scratch));
    cpu_mirrors_to_p(cpu);
    solid.ra = cpu->A; solid.rx = cpu->X; solid.ry = cpu->Y; solid.rp = cpu->P; solid.rdb = cpu->DB;
    body_select(other(), false);
    solid.other_04 = g_ram[PLAYER + 0x2c];
    *top = solid.entry_2c;
    cpu->A = solid.a; cpu->X = solid.x; cpu->Y = solid.y; cpu->P = solid.p; cpu->DB = solid.db;
    cpu_p_to_mirrors(cpu);
    solid.pass = 2;
    interp_bridge_pre_opcode_redirect(solid.entry);
    return;
  }
  if (debug_on() && g_ram[PLAYER + 0x2c] != solid.other_04)
    fprintf(stderr, "[x2coop] solid %04X (id %02X) holds P%u\n", solid.d,
            g_ram[(uint16_t)(solid.d + 10)], other() + 1);
  if (solid.entry == SOLID_CC7F && !(solid.entry_2c & 1))
    carry_note(solid.d, (uint8_t)((solid.first_2c & 1 ? 1u << s.main : 0) | (*top & 1 ? 1u << other() : 0)));
  *top |= solid.first_2c;
  /* The caller goes on with player 1's distances. */
  memcpy(g_ram, solid.scratch, sizeof(solid.scratch));
  body_select(s.main, false);
  settle();
  cpu->A = solid.ra; cpu->X = solid.rx; cpu->Y = solid.ry; cpu->P = solid.rp; cpu->DB = solid.rdb;
  cpu_p_to_mirrors(cpu);
  solid.pass = 0;
}
/* The generated code runs $08:D865 interpreted while a second seat could
 * answer (its entry and returns are hooked below). */
int MmxX2CoopOverlapInterp(void) { return s.ov == 3 || overlap_ready(); }

/* Player 2 kept in for an object: back to the main seat. */
void MmxX2CoopObjectDone(void) {
  if (s.ov != 3) return;
  body_select(s.main, false);
  settle();
  s.ov = 0;
}

static void overlap_hook(CpuState *cpu, uint32_t pc) {
  if ((pc & 0x7fffff) == OVERLAP_ENTRY) {
    if (s.ov == 3 && cpu->D != s.ov_d) MmxX2CoopObjectDone();
    if (s.ov || cpu->X != PLAYER || !overlap_ready()) return;
    s.ov = 1;
    s.ov_s = cpu->S; s.ov_d = cpu->D;
    s.ov_a = cpu->A; s.ov_x = cpu->X; s.ov_y = cpu->Y; s.ov_db = cpu->DB;
    cpu_mirrors_to_p(cpu);
    s.ov_p = cpu->P;
    s.ov_return = (uint32_t)g_ram[(uint16_t)(cpu->S + 1)] | (uint32_t)g_ram[(uint16_t)(cpu->S + 2)] << 8 |
        (uint32_t)g_ram[(uint16_t)(cpu->S + 3)] << 16;
    return;
  }
  if ((s.ov != 1 && s.ov != 2) || cpu->S != s.ov_s || cpu->D != s.ov_d) return;
  const bool hit = cpu->_flag_C != 0;
  if (s.ov == 1) {
    if (hit) { s.ov = 0; return; }
    /* Player 1 missed: the same test for player 2. */
    body_select(other(), false);
    s.ov = 2;
    cpu->A = s.ov_a; cpu->X = s.ov_x; cpu->Y = s.ov_y; cpu->DB = s.ov_db; cpu->P = s.ov_p;
    cpu_p_to_mirrors(cpu);
    interp_bridge_pre_opcode_redirect(OVERLAP_ENTRY);
    return;
  }
  if (!hit) {
    body_select(s.main, false);
    settle();
    s.ov = 0;
    return;
  }
  if (door_caller(s.ov_return)) {
    /* Player 2 at a boss door: the main seat, so the door's walk, scroll
     * and boss are its; player 1 goes out and beams in after. */
    const unsigned leaving = s.main;
    s.ov = 0;
    hand_main(other());
    s.seats[leaving].status = SEAT_ABSENT;
    s.seats[leaving].beam_next = 1;
    if (debug_on()) fprintf(stderr, "[x2coop] P%u takes the door\n", s.main + 1);
    return;
  }
  /* Player 2 is the one touched: kept in for the rest of this object. */
  s.ov = 3;
  if (debug_on())
    fprintf(stderr, "[x2coop] object %04X (id %02X, from %06X) touches P%u\n", s.ov_d,
            g_ram[(uint16_t)(s.ov_d + 10)], (unsigned)s.ov_return, other() + 1);
}

/* Health and Heart Tank pickups run over several frames: the touch (the
 * collector live) freezes the world and holds the player ($08:DC64: action
 * $18, $0A46 set), later states refill $09FF a unit at a time and release
 * the player ($08:DC9C). Those later states never test the player again, so
 * they acted on player 1: player 2 kept its health and stayed held, unable to
 * move. At their entries the held seat is put in for the rest of the item
 * (as a touch keeps it in): the one held while the other is not. */
enum { HELD = 0x6e }; /* $0A46: held by an item */
static const uint32_t kHeldStates[] = {
  0x01d62a, 0x01d65f, /* health: refill, release */
  0x01e4bd, 0x01e4ca, 0x01e502, /* Heart Tank: wait, refill, release */
};
static void held_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (s.ov || !overlap_ready() || g_ram[PLAYER + HELD] || !s.seats[other()].body[HELD]) return;
  body_select(other(), false);
  s.ov = 3;
  s.ov_d = cpu->D;
  if (debug_on()) fprintf(stderr, "[x2coop] item %04X goes on with P%u\n", cpu->D, other() + 1);
}

/* The beam-in's end ($08:A624: JSL $00:E67B) sets the camera's top limit
 * ($1E6A/$1E78) from the checkpoint's table: a stage's start. A co-op
 * arrival (the other seat on the stage) keeps the camera as it is: from the
 * checkpoint's table past a boss door it left the camera between two rooms
 * (Flame Stag's: 1024 against 544, every other frame). */
int MmxX2CoopBeamCameraSkip(void) {
  if (!s.active || !gameplay()) return 0;
  const unsigned them = s.live ^ 1u;
  return s.seats[them].status == SEAT_ALIVE && (seat_body(them)[0x27] & 127) &&
      seat_body(them)[2] != ACTION_DEATH;
}
static void beam_camera_hook(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc;
  if (MmxX2CoopBeamCameraSkip()) interp_bridge_pre_opcode_redirect(0x08a62c);
}

/* The same from the generated code (tools/apply_xtreme_tank_hooks.py). */
void MmxX2CoopHeldState(CpuState *cpu) { if (s.active) held_hook(cpu, 0); }

/* Wheel Gator's oil ($05:8639, the room's floor object, D = the object): at
 * its RTL ($05:8654) player 1 has been held on the oil's surface by $05:87AF
 * ($09DD/$09E0 only; skipped while $1F54 is set, the Gator holding player 1).
 * The same for the other seat, unless the Gator holds it (+$64). */
void MmxX2CoopOilFloor(CpuState *cpu) {
  if (!s.active || s.live != s.main || s.pass || !gameplay() || g_ram[0xd2] != 4) return;
  const unsigned o = other();
  if (s.seats[o].status != SEAT_ALIVE) return;
  uint8_t *b = s.seats[o].body;
  if (!(b[0x27] & 127) || b[2] == ACTION_DEATH || b[0x64]) return;
  /* $05:863D: a jump off the oil (new buttons $30) latches +$AB. */
  if (g_ram[(uint16_t)(cpu->D + 1)] && !b[0xab] && (b[0x3a] & 0x30)) b[0xab] = 4;
  /* $05:87AF/$87CC: the surface under the feet, in low bytes as there. */
  const uint8_t feet = (uint8_t)(b[8] + 0x14);
  const uint8_t i = (uint8_t)((uint8_t)((b[5] >> 1) + g_ram[0x1f57]) & 0x7f);
  const uint8_t surface = (uint8_t)(cpu_read8(cpu, cpu->DB, (uint16_t)(0x6b00 + i)) + 0xd0);
  if (surface >= feet) return;
  b[8] = (uint8_t)(surface - 0x10);
  b[0x2c] |= 4;
}
static void oil_hook(CpuState *cpu, uint32_t pc) { (void)pc; MmxX2CoopOilFloor(cpu); }

static void object_done_hook(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc;
  MmxX2CoopObjectDone();
}

/* $00:E0EA: the main seat's feet are below the camera's bottom, which kills
 * it. With the shared camera above, only below the room's lowest camera. */
int MmxX2CoopMainBelowOk(void) {
  if (!shared_screen()) return 0;
  return (int16_t)(word(g_ram + PLAYER + 8) - 32 - (word(g_ram + 0x1e6c) + 224)) < 0;
}

int MmxX2CoopZeroSeat(void) { return s.active ? (int)s.zero_owner : -1; }
bool MmxX2CoopLiveIsX(void) {
  return s.active && s.seats[s.live].character == MMX_COOP_X;
}

/* A new seat's state; the live state is left as it was. */
static MmxZeroState fresh_zero(bool x) {
  const MmxZeroState live = MmxZeroGetState();
  MmxZeroResetState();
  MmxZeroState z = MmxZeroGetState();
  MmxZeroSelectState(live);
  z.active_x = x ? 1 : 0;
  return z;
}

void MmxX2CoopEnter(void) {
  s.configured = MmxCoopEnabled() && MmxZeroEnabled();
  s.active = false;
  if (!s.configured) return;
  const MmxCoopState coop = MmxCoopGetState();
  const MmxZeroState keep = MmxZeroGetState();
  memset(&s.seats, 0, sizeof(s.seats));
  for (unsigned i = 0; i < 2; ++i) {
    s.seats[i].character = coop.players[i].character;
    s.seats[i].zero = fresh_zero(coop.players[i].character == MMX_COOP_X);
  }
  /* Two of one character (an Axl seat in X1) play as X and Zero here. */
  if (s.seats[0].character == s.seats[1].character) {
    s.seats[1].character = s.seats[0].character ^ 1;
    s.seats[1].zero = fresh_zero(s.seats[1].character == MMX_COOP_X);
  }
  (void)keep;
  s.live = 0;
  s.zero_owner = 0;
  s.main = 0;
  s.other_shots = s.shots_before = 0;
  MmxZeroSetState(s.seats[0].zero);
  s.pass = s.contact = 0;
  s.active = true;
  settle();
  fprintf(stderr, "[x2coop] co-op on the X2 page: P1 %s, P2 %s\n",
          s.seats[0].character == MMX_COOP_ZERO ? "Zero" : "X",
          s.seats[1].character == MMX_COOP_ZERO ? "Zero" : "X");
}

void MmxX2CoopLeave(void) {
  if (!s.active) return;
  body_select(0, true);
  s.main = 0;
  zero_select(0);
  MmxX2ZeroSetPlayerSource(NULL);
  MmxCoopSetSeatOverride(false, 0, 0, 0);
  s.active = false;
}

/* X2's $00:E52E for a pad that is not port 1: held, previous and new
 * actions in the body (+$36/+$38/+$3A). */
static void apply_input(uint8_t *body, Seat *seat, uint16_t input) {
  /* Zero's L / R are his own (KSX's list, through the seat's pad): X2's
   * weapon change would switch X's weapon and palette. */
  if (seat->character == MMX_COOP_ZERO) input &= (uint16_t)~(PAD_L | PAD_R);
  unsigned native = 0;
  for (unsigned bit = 0; bit < 12; ++bit)
    if (input & (1u << bit)) native |= 0x8000u >> bit;
  const unsigned buttons = ((native & 255) >> 2 & 0x3c) | (native >> 8 & 0xc0) | (native >> 12 & 3);
  static const uint16_t action_bits[6] = {0x4000, 0x8000, 0x0080, 0x0020, 0x0010, 0x1000};
  /* The pad's directions, and Start: X2's pause ($00:9F50) reads it from
   * the main seat's new buttons ($0A13 bit 4), player 2's when it is the
   * main seat. */
  unsigned actions = native & 0x1f00;
  for (unsigned i = 0; i < 6; ++i) {
    const unsigned mask = g_ram[0xffc0 + i];
    if (mask && (buttons & mask) == mask) actions |= action_bits[i];
  }
  const unsigned previous = seat->actions;
  seat->actions = (uint16_t)actions;
  put_word(body + 0x38, previous);
  put_word(body + 0x36, actions);
  /* Start pressed this frame (the frame's own edge: the main pass's set-up
   * runs twice a frame, and the second saw it as held). */
  put_word(body + 0x3a, (actions & ~previous) | (seat->pressed & PAD_START ? 0x1000u : 0));
}

/* The NMI's joypad copies (held $A8, previous $AA, new $AC), which Zero's
 * code reads, as `seat`'s. */
static void seat_joypad(const Seat *seat) {
  unsigned held = 0, fresh = 0;
  for (unsigned bit = 0; bit < 12; ++bit) {
    if (seat->pad & (1u << bit)) held |= 0x8000u >> bit;
    if (seat->pressed & (1u << bit)) fresh |= 0x8000u >> bit;
  }
  put_word(g_ram + 0xa8, held);
  put_word(g_ram + 0xaa, held & ~fresh);
  put_word(g_ram + 0xac, fresh);
}

/* Player 1 stands in ordinary control: a place to bring player 2 in. */
static bool anchor_ready(const uint8_t *p1) {
  const unsigned action = p1[2];
  return p1[1] == 2 && (p1[0x27] & 127) && action <= 0x16 && action != ACTION_DEATH &&
      action != 0x0e && !g_ram[0x1f2b] && !g_ram[0x1f25];
}

/* A seat arrives beside the main seat, from a copy of its body. */
/* A revived player beams in as at a stage's start (X2's action $22: set
 * up, fall, land), from the top of the screen onto ground beside the other
 * player at its level, as X1 co-op's respawn lands: the nearer side first,
 * 32 to 96 px away, else where the other player stands. */
enum { ACTION_BEAM_IN = 0x22 };
static bool floor_at(int x, int top, int bottom, int *floor) {
  for (int y = top; y < bottom; y += 2) {
    int surface = 0;
    if (MmxWeaponsTerrainSolid(g_ram, x, y, true, &surface)) {
      /* Room for the body above it. */
      for (int h = surface - 40; h < surface; h += 8)
        if (MmxWeaponsTerrainSolid(g_ram, x, h, true, NULL)) return false;
      *floor = surface;
      return true;
    }
  }
  return false;
}
static void beam_in(uint8_t *b, const uint8_t *anchor) {
  const int ax = (int)word(anchor + 5), feet = (int)word(anchor + 8) + 16;
  const int cam_x = (int)word(g_ram + 0x1e5d), cam_y = (int)word(g_ram + 0x1e60);
  const bool left_first = (anchor[0x11] & 0x40) == 0;
  int x = ax;
  for (int n = 32; n <= 96 && x == ax; n += 8)
    for (int k = 0; k < 2; ++k) {
      const int cx = ax + ((k == 0) == left_first ? n : -n);
      int floor;
      if (cx < cam_x + 16 || cx > cam_x + 240) continue;
      if (floor_at(cx, cam_y + 48, cam_y + 216, &floor) && floor >= feet - 24 && floor <= feet + 24) {
        x = cx;
        break;
      }
    }
  /* From the top of the screen, unless terrain over the landing spot would
   * catch the beam above the partner's level (the structure over a boss
   * door, a low corridor): then at the partner's own height, where there
   * is room for a body. */
  int y = cam_y + 8;
  for (int h = cam_y + 8; h < feet - 24; h += 8)
    if (MmxWeaponsTerrainSolid(g_ram, x, h, true, NULL)) { y = (int)word(anchor + 8); break; }
  put_word(b + 5, (unsigned)x); put_word(b + 0x22, (unsigned)x);
  put_word(b + 8, (unsigned)y); put_word(b + 0x24, (unsigned)y);
  put_word(b + 0x1a, 0);
  b[2] = ACTION_BEAM_IN;
  b[3] = 0;
}

static void join(unsigned seat) {
  const uint8_t *p1 = g_ram + PLAYER;
  Seat *p = &s.seats[seat];
  memcpy(p->body, p1, BODY);
  memset(p->helpers, 0, sizeof(p->helpers));
  const bool facing_left = (p1[0x11] & 0x40) == 0;
  const int x = (int)word(p1 + 5) + (facing_left ? 20 : -20);
  put_word(p->body + 5, (unsigned)x);
  put_word(p->body + 0x22, (unsigned)x);
  const unsigned max = g_ram[0x1fd1] ? g_ram[0x1fd1] : 16;
  p->body[0x27] = (uint8_t)((max & 127) | (p1[0x27] & 128));
  /* No inherited invulnerability blink or hurt state. */
  p->body[0x88] = p->body[0x89] = p->body[0x8a] = p->body[0x8b] = 0;
  p->zero = fresh_zero(p->character == MMX_COOP_X);
  if (s.zero_owner == seat) MmxZeroSelectState(p->zero);
  p->status = SEAT_ALIVE;
  p->wait = p->away = 0;
  if (debug_on()) fprintf(stderr, "[x2coop] P%u joins at x=%d\n", seat + 1, x);
  (void)x;
  diag_event("join", seat);
}

/* Shots the other seat fired that have ended (before the main pass): X2's
 * shot code took them off the main seat's count ($0A0D, live at the time),
 * though they were the other's. Only the shot routines that count take a
 * shot off (Zero's saber objects don't), so the decrements are measured:
 * what the main seat's own ended shots don't explain goes back to it. */
static int bit_count(unsigned v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }

static void settle_shots(void) {
  const uint16_t used = shot_slots();
  const uint16_t ended_other = (uint16_t)(s.other_shots & ~used);
  const uint16_t ended_main = (uint16_t)(s.mark_slots & ~s.other_shots & ~used);
  s.other_shots &= (uint16_t)~ended_other;
  uint8_t *main_count = g_ram + PLAYER + 0x35, *other_count = s.seats[other()].body + 0x35;
  if (!s.mark_valid) {
    if ((int8_t)*main_count < 0) *main_count = 0;
    return;
  }
  s.mark_valid = 0;
  /* Signed bytes: a decrement from 0 (the other's shot ending while the
   * main seat had none out) leaves $FF, one below the mark, not 255 above. */
  int drop = (int8_t)(uint8_t)(s.count_mark - *main_count);
  drop -= bit_count(ended_main);
  if (drop > bit_count(ended_other)) drop = bit_count(ended_other);
  for (; drop > 0; --drop) {
    if ((int8_t)*main_count < 0x7f) ++*main_count;
    if (*other_count && *other_count < 0x80) --*other_count;
  }
  /* Never fewer than none out: $FF would stop the buster for good. */
  if ((int8_t)*main_count < 0) *main_count = 0;
}

/* The main role to `seat` (outside the passes): its body into $09D8. */
static void hand_main(unsigned seat) {
  if (seat == s.main) return;
  body_select(seat, true);
  s.main = (uint8_t)seat;
  s.other_shots = 0;
  s.mark_valid = 0;
  settle();
  if (debug_on()) fprintf(stderr, "[x2coop] P%u is the main seat\n", seat + 1);
}

void MmxX2CoopFrameBegin(void) {
  if (!s.active) return;
  for (unsigned i = 0; i < 2; ++i) {
    const uint16_t pad = RtlGetPadState((int)i);
    s.seats[i].pressed = (uint16_t)(pad & ~s.seats[i].pad);
    s.seats[i].pad = pad;
  }
  MmxCoopSetSeatOverride(true, s.zero_owner, s.seats[s.zero_owner].pad, s.seats[s.zero_owner].pressed);
  /* A lag frame (a mini boss's arrival) can end inside a pass: X2's routine
   * yields at the frame's end and goes on in the next one. The pass, with
   * the other seat's body in $09D8, stays as it is until its own end (the
   * terrain pass's flag cleared here once left player 2 in player 1's place
   * for good). */
  if (s.live != s.main) {
    if (gameplay() && g_ram[0xd2] == 4) return;
    /* Out of the stage mid-pass: the main seat back in. */
    body_select(s.main, s.pass != 0);
    s.pass = s.ov = 0;
    settle();
  }
  s.contact = s.terrain = 0;
  if (!gameplay() || g_ram[0xd2] != 4) {
    /* Stage select, a stage's start or a restart: player 1 is the main seat
     * again (X2 sets its player up afresh) and player 2 joins again. */
    if (s.seats[other()].status != SEAT_ABSENT && debug_on())
      fprintf(stderr, "[x2coop] P%u leaves (mode %02X %02X %02X)\n", other() + 1, g_ram[0xd0], g_ram[0xd1], g_ram[0xd2]);
    if (s.main != 0) {
      s.main = 0;
      s.live = 0;
      settle();
    }
    s.seats[0].status = SEAT_ALIVE;
    s.seats[1].status = SEAT_ABSENT;
    s.seats[0].beam_next = s.seats[1].beam_next = 0;
    s.other_shots = 0;
    s.mark_valid = 0;
    return;
  }
  const unsigned o = other();
  Seat *p2 = &s.seats[o];
  if (p2->status == SEAT_FALLEN && p2->wait) --p2->wait;
  /* Player 2 arrives on its own; a fallen seat comes back with Select, for
   * an extra life ($1FB3), once its death has played out. The press waits
   * for the other to be ready (not hurt, not in a door or an event). */
  if (p2->status == SEAT_FALLEN && !p2->wait && (p2->pressed & PAD_SELECT)) p2->beam_next = 1;
  const bool arrives = p2->status == SEAT_ABSENT && (o == 1 || p2->beam_next);
  const bool revives = p2->status == SEAT_FALLEN && !p2->wait && p2->beam_next && g_ram[0x1fb3];
  if ((arrives || revives) && anchor_ready(g_ram + PLAYER) && !g_ram[0x1f1d] && !g_ram[0x1f36]) {
    const bool beam = revives || p2->beam_next;
    join(o);
    p2->beam_next = 0;
    if (revives) --g_ram[0x1fb3];
    if (beam) beam_in(s.seats[o].body, g_ram + PLAYER);
    /* Player 1 back: the main seat again (X2 draws its own player). */
    if (o == 0) hand_main(0);
  }
}

bool MmxX2CoopSplit(void) {
  return s.active && MmxCoopViewsLocalSplit() && !MmxNetplayActive();
}

/* Two cameras: Side by side locally, Netplay cameras: Independent online
 * (every peer runs the same rules: the leash and object window are game
 * state; which camera a peer shows is not). */
bool MmxX2CoopIndependent(void) {
  return s.active && (MmxNetplayActive() ? MmxCoopViewsIndependent() : MmxCoopViewsLocalSplit());
}

int MmxX2CoopSplitMargin(void) {
  return MmxX2CoopIndependent() && gameplay() ? LEASH_X + 32 : 0;
}

/* Unified: player 2 stays within player 1's camera. Side by side: within
 * the leash around it (and the room's camera limits), so player 2's own
 * camera never leaves the area where X2 keeps objects alive. */
static void constrain(uint8_t *body, Seat *p) {
  const int cx = (int)word(g_ram + 0x1e5d), cy = (int)word(g_ram + 0x1e60);
  const bool split = MmxX2CoopIndependent();
  int left = cx + 12, right = cx + 244, top = cy - 48, bottom = cy + 224 + 48;
  if (split) {
    const int room_left = (int)word(g_ram + 0x1e66), room_right = (int)word(g_ram + 0x1e68) + 256;
    left = (cx - LEASH_X > room_left ? cx - LEASH_X : room_left) + 12;
    right = (cx + 256 + LEASH_X < room_right ? cx + 256 + LEASH_X : room_right) - 12;
    if (right < left) right = left;
    top -= LEASH_Y; bottom += LEASH_Y;
  }
  int x = (int)word(body + 5);
  if (!split) {
    /* Within one screen of the main seat (the camera shows their midpoint). */
    const int mx = (int)word(s.seats[s.main].body + 5);
    if (left < mx - APART) left = mx - APART;
    if (right > mx + APART) right = mx + APART;
  }
  if (x < left) x = left;
  if (x > right) x = right;
  put_word(body + 5, (unsigned)x);
  const int y = (int)word(body + 8);
  const bool off = y < top || y > bottom;
  p->away = off ? (uint16_t)(p->away + 1) : 0;
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Side by side: player 1's camera and player 2's, which follows player 2
 * with X2's dead zone inside the room's camera limits and the leash, and
 * eases back to player 1's while player 2 is out of play. Once per drawn
 * frame. False outside a stage (both panes show the shared frame). */
bool MmxX2CoopSplitCameras(int *c1x, int *c1y, int *c2x, int *c2y) {
  const int cx = (int)word(g_ram + 0x1e5d), cy = (int)word(g_ram + 0x1e60);
  *c1x = *c2x = cx; *c1y = *c2y = cy;
  if (!MmxX2CoopIndependent() || !gameplay() || g_ram[0xd2] != 4) { s.cam2_valid = false; return false; }
  if (!s.cam2_valid) { s.cam2_x = cx; s.cam2_y = cy; s.cam2_valid = true; }
  int tx = cx, ty = cy;
  const Seat *p = &s.seats[1];
  if (p->status == SEAT_ALIVE) {
    const uint8_t *b = seat_body(1);
    const int px = (int)word(b + 5), py = (int)word(b + 8);
    tx = s.cam2_x; ty = s.cam2_y;
    if (px - tx < 0x70) tx = px - 0x70;
    if (px - tx > 0x90) tx = px - 0x90;
    if (py - ty < 0x60) ty = py - 0x60;
    if (py - ty > 0xa0) ty = py - 0xa0;
    tx = clamp_int(tx, (int)word(g_ram + 0x1e66), (int)word(g_ram + 0x1e68));
    ty = clamp_int(ty, (int)word(g_ram + 0x1e6a), (int)word(g_ram + 0x1e6c));
    tx = clamp_int(tx, cx - LEASH_X, cx + LEASH_X);
    ty = clamp_int(ty, cy - LEASH_Y, cy + LEASH_Y);
  }
  int *axis[2] = {&s.cam2_x, &s.cam2_y};
  const int target[2] = {tx, ty};
  for (unsigned i = 0; i < 2; ++i) {
    const int d = target[i] - *axis[i];
    if (d > 256 || d < -256 || (d >= -8 && d <= 8)) *axis[i] = target[i];
    else *axis[i] += d / 4 + (d > 0 ? 2 : -2);
  }
  *c2x = s.cam2_x; *c2y = s.cam2_y;
  return true;
}

int MmxX2CoopPassEnd(CpuState *cpu) {
  if (!s.active) return 0;
  if (s.pass == 0) {
    Seat *p2 = &s.seats[other()];
    if (s.live != s.main) return 0;
    s.count_mark = g_ram[PLAYER + 0x35];
    s.mark_slots = shot_slots();
    s.mark_valid = 1;
    /* The main seat's death while the other plays on: the other takes the
     * main role, and this body plays its death out as the other seat. */
    if (g_ram[PLAYER + 2] == ACTION_DEATH && p2->status == SEAT_ALIVE && gameplay() &&
        g_ram[0xd2] == 4 && (p2->body[0x27] & 127)) {
      if (s.main != 0) memcpy(g_ram + 0xa8, s.joypad_nmi, sizeof(s.joypad_nmi));
      s.seats[s.main].status = SEAT_ALIVE;  /* falls when its death plays out */
      memcpy(g_ram + FREEZE, freeze_main, FREEZE_SIZE);
      hand_main(other());
      return 0;
    }
    /* The main seat's held actions ($18 frozen: Sub Tanks, Heart Tanks, boss
     * intros and a door's start; $1A-$1E) and the world's freezes hold the
     * other. Only a boss door's walk ($1C, through the door) sends it out, to
     * beam in on the far side: a pickup's freeze never did. */
    const unsigned a1 = g_ram[PLAYER + 2];
    if (a1 == 0x1c && p2->status == SEAT_ALIVE && gameplay() && g_ram[0xd2] == 4) {
      p2->status = SEAT_ABSENT;
      p2->beam_next = 1;
      if (debug_on()) fprintf(stderr, "[x2coop] P%u leaves for the door\n", other() + 1);
    }
    if (p2->status != SEAT_ALIVE || !gameplay() || g_ram[0xd2] != 4 ||
        (a1 >= 0x18 && a1 <= 0x1e) || (g_ram[0x1f2b] && p2->body[2] != ACTION_DEATH)) {
      if (s.main != 0) memcpy(g_ram + 0xa8, s.joypad_nmi, sizeof(s.joypad_nmi));
      settle();
      return 0;
    }
    /* Unified: the main seat within one screen of the other. */
    if (!MmxX2CoopIndependent()) {
      const int ox = (int)word(p2->body + 5);
      const int x = (int)word(g_ram + PLAYER + 5);
      const int limited = x < ox - APART ? ox - APART : x > ox + APART ? ox + APART : x;
      if (limited != x) { put_word(g_ram + PLAYER + 5, (unsigned)limited); put_word(g_ram + PLAYER + 0x1a, 0); }
    }
    s.a = cpu->A; s.x = cpu->X; s.y = cpu->Y; s.db = cpu->DB;
    /* Undo the prologue's PHP/PHD: $08:9428 pushes them again. */
    const uint16_t d = (uint16_t)(g_ram[(uint16_t)(cpu->S + 1)] | g_ram[(uint16_t)(cpu->S + 2)] << 8);
    const uint8_t p = g_ram[(uint16_t)(cpu->S + 3)];
    cpu->S = (uint16_t)(cpu->S + 3);
    cpu->D = d;
    cpu->P = p;
    cpu_p_to_mirrors(cpu);
    s.p = p;
    s.restart = g_ram[0x1f1c]; s.restart_f = g_ram[0x1f1f]; s.restart_g = g_ram[0x1f20];
    memcpy(freeze_other, g_ram + FREEZE, FREEZE_SIZE);
    memcpy(s.palette, g_ram + 0x300, sizeof(s.palette));
    s.shots_before = shot_slots();
    const unsigned o = other();
    body_select(o, true);
    zero_select(o);
    MmxX2ZeroSetPlayerSource(NULL);
    uint8_t *b = g_ram + PLAYER;
    apply_input(b, p2, p2->pad);
    /* The NMI's joypad copies as the other seat's. */
    memcpy(s.joypad, g_ram + 0xa8, sizeof(s.joypad));
    seat_joypad(p2);
    /* Prepared outside the routine for player 1 ($00:D1F3 in X1): the
     * previous position and this frame's fire command. */
    memcpy(b + 0x22, b + 5, 2);
    memcpy(b + 0x24, b + 8, 2);
    g_ram[0x1f1e] = 0;
    s.pass = 2;
    return 1;
  }
  /* The other seat's pass ends. */
  uint8_t *b = g_ram + PLAYER;
  Seat *p2 = &s.seats[other()];
  /* Shots it fired are counted on its own body. */
  s.other_shots |= (uint16_t)(shot_slots() & ~s.shots_before);
  /* Player 2's counterpart of $00:CEA8 (STZ $0A04, X1 $00:D21A). */
  b[0x2c] = 0;
  if (debug_on()) {
    static unsigned n;
    if (n++ % 30 == 0)
      fprintf(stderr, "[x2coop] P2 act=%02X/%02X x=%u y=%u hp=%u in=%04X 2b=%02X 5e=%02X vy=%04X 1d=%02X sl=%u ch=%u ax=%u | P1 x=%u y=%u cam=%u,%u\n",
              b[2], b[3], word(b + 5), word(b + 8), b[0x27] & 127, word(b + 0x36),
              b[0x2b], b[0x5e], word(b + 0x1a), b[0x1d], MmxZeroGetState().slash, MmxZeroGetState().charge, MmxZeroGetState().active_x,
              word(s.seats[0].body + 5), word(s.seats[0].body + 8),
              word(g_ram + 0x1e5d), word(g_ram + 0x1e60));
  }
  if (b[2] == ACTION_DEATH) {
    /* The other seat's death never restarts the stage. */
    g_ram[0x1f1c] = s.restart; g_ram[0x1f1f] = s.restart_f; g_ram[0x1f20] = s.restart_g;
    memcpy(g_ram + 0x300, s.palette, sizeof(s.palette));
    memcpy(g_ram + FREEZE, freeze_other, FREEZE_SIZE);
    if (b[3] >= 6 && p2->status != SEAT_FALLEN) {
      p2->status = SEAT_FALLEN;
      p2->beam_next = 0;
      p2->wait = RESPAWN_FRAMES;
      if (debug_on()) fprintf(stderr, "[x2coop] P%u fell\n", other() + 1);
      diag_event("fell", other());
    }
  } else {
    /* Zero's seat is drawn by the bridge from X's palette's neighbours:
     * whatever its pass wrote to the palette buffer is X's. */
    if (p2->character == MMX_COOP_ZERO) memcpy(g_ram + 0x300, s.palette, sizeof(s.palette));
    constrain(b, p2);
    /* Below the room's lowest camera: a pit (X1 $00:E12D's check for the
     * partner). */
    if ((b[0x27] & 127) && (int16_t)(word(b + 8) - 32 - (word(g_ram + 0x1e6c) + 224)) >= 0) {
      ++b[0x30]; b[0x2f] = 8; b[0x26] = 127;
      b[2] = b[0x6a] = ACTION_DEATH; b[3] = 0; b[0x27] = 128;
      if (debug_on()) fprintf(stderr, "[x2coop] P%u falls into a pit\n", other() + 1);
    }
    if (p2->away >= AWAY_FRAMES && other() == 1) {
      /* Left behind off screen: back beside player 1. */
      p2->status = SEAT_ABSENT;
      p2->beam_next = 1;
      if (debug_on()) fprintf(stderr, "[x2coop] P2 left behind; returning\n");
      diag_event("left-behind", 1);
    }
  }
  memcpy(g_ram + 0xa8, s.main != 0 ? s.joypad_nmi : s.joypad, sizeof(s.joypad));
  body_select(s.main, true);
  settle();
  cpu->A = s.a; cpu->X = s.x; cpu->Y = s.y; cpu->DB = s.db;
  s.pass = 0;
  return 0;
}

/* $08:C314 (after the object loops, D = $09D8): the player's terrain
 * contact (+$2B, landing and walls) via $08:CD5E. At its PLD ($08:C325) the
 * PHD is undone and it runs again for player 2's body. */
int MmxX2CoopTerrainEnd(CpuState *cpu) {
  if (!s.active || s.pass) return 0;
  MmxX2CoopObjectDone();
  if (s.terrain) {
    if (g_ram[PLAYER + 2] == ACTION_DEATH || !(g_ram[PLAYER + 0x27] & 127))
      memcpy(g_ram + FREEZE, freeze_other, FREEZE_SIZE);
    body_select(s.main, false);
    settle();
    s.terrain = 0;
    return 0;
  }
  if (s.live != s.main || s.seats[other()].status != SEAT_ALIVE || !gameplay() || g_ram[0xd2] != 4) return 0;
  cpu->D = (uint16_t)(g_ram[(uint16_t)(cpu->S + 1)] | g_ram[(uint16_t)(cpu->S + 2)] << 8);
  cpu->S = (uint16_t)(cpu->S + 2);
  memcpy(freeze_other, g_ram + FREEZE, FREEZE_SIZE);
  body_select(other(), false);
  zero_select(other());
  MmxX2ZeroSetPlayerSource(NULL);
  s.terrain = 1;
  return 1;
}

static void terrain_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (MmxX2CoopTerrainEnd(cpu)) interp_bridge_pre_opcode_redirect(0x08c314);
}

/* Enemy contact with the player ($08:D752): player 2's turn after a miss. */
static const uint32_t kContactReturns[] = {0x08d799, 0x08d7d4, 0x08d962, 0x08da14,
                                           0x08da6e, 0x08da71, 0x08da97, 0x08da9a};

/* Entry: player 1's pass begins (it runs first, as in retail). */
void MmxX2CoopContactBegin(CpuState *cpu) {
  if (!s.active || s.pass || s.contact || s.live != s.main ||
      s.seats[other()].status != SEAT_ALIVE || !gameplay()) return;
  s.contact = 1;
  s.contact_s = cpu->S;
  s.contact_d = cpu->D;
  cpu_mirrors_to_p(cpu);
  s.contact_entry_p = cpu->P;
}

/* At each RTL: after player 1's pass, nonzero restarts the routine at its
 * entry for player 2 (entry flags restored); after player 2's, player 1 is
 * put back and the first contact found is the result. */
int MmxX2CoopContactEnd(CpuState *cpu) {
  if (!s.contact || cpu->S != s.contact_s || cpu->D != s.contact_d) return 0;
  if (s.contact == 1) {
    s.contact_a = cpu->A; s.contact_x = cpu->X; s.contact_y = cpu->Y;
    s.contact_db = cpu->DB;
    cpu_mirrors_to_p(cpu);
    s.contact_p = cpu->P;
    body_select(other(), false);
    zero_select(other());
    MmxX2ZeroSetPlayerSource(NULL);
    s.contact = 2;
    cpu->P = s.contact_entry_p;
    cpu_p_to_mirrors(cpu);
    return 1;
  }
  const bool first_hit = (s.contact_a & 255) != 0, second_miss = !(cpu->A & 255);
  if (debug_on() && !second_miss)
    fprintf(stderr, "[x2coop] enemy %04X touches P2 (hp %u)\n", s.contact_d, g_ram[PLAYER + 0x27] & 127);
  if (!second_miss) diag_event("enemy-contact", other());
  body_select(s.main, false);
  settle();
  if (first_hit || second_miss) {
    cpu->A = s.contact_a; cpu->X = s.contact_x; cpu->Y = s.contact_y;
    cpu->DB = s.contact_db; cpu->P = s.contact_p;
    cpu_p_to_mirrors(cpu);
  }
  s.contact = 0;
  return 0;
}

/* Interpreter twins (the M0 entries run interpreted). */
static void contact_hook(CpuState *cpu, uint32_t pc) {
  if ((pc & 0x7fffff) == CONTACT_ENTRY) MmxX2CoopContactBegin(cpu);
  else if (MmxX2CoopContactEnd(cpu)) interp_bridge_pre_opcode_redirect(CONTACT_ENTRY);
}

/* $08:C9B8 uploads the live body's pose tiles to X's VRAM. Zero's seat is
 * drawn by the bridge, and its upload would replace X's tiles. */
int MmxX2CoopSkipChr(void) {
  return s.active && s.live != s.main && s.seats[s.live].character == MMX_COOP_ZERO &&
      s.seats[s.main].character == MMX_COOP_X;
}

static void chr_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  (void)cpu;
  if (MmxX2CoopSkipChr()) interp_bridge_pre_opcode_redirect(0x08c9b7); /* the RTL before it */
}

/* Unified camera ($00:DE9E / $00:DEBD read the player's X / Y; the
 * generated-code hooks and the interpreter twins below): the midpoint of
 * both seats, rounded toward the main seat, while both are on the shared
 * screen, as X1's co-op camera ($00:DE9D) does. axis: 5 X, 8 Y. */
static bool shared_screen(void) {
  if (!s.active || MmxX2CoopIndependent() || !gameplay() || g_ram[0xd2] != 4 ||
      s.live != s.main || s.pass) return false;
  const Seat *o = &s.seats[other()];
  /* Boss doors and event locks ($1F1D, $1F36) keep the main seat's camera. */
  return o->status == SEAT_ALIVE && o->body[2] != ACTION_DEATH && (o->body[0x27] & 127) &&
      g_ram[PLAYER + 2] != ACTION_DEATH && !g_ram[0x1f1d] && !g_ram[0x1f36];
}

unsigned MmxX2CoopCameraAxis(unsigned value, unsigned axis) {
  if (!shared_screen()) return value;
  const int a = (int)(int16_t)value, b = (int)(int16_t)word(s.seats[other()].body + axis);
  return (unsigned)(a + (b - a) / 2) & 0xffff;
}

static void camera_hook(CpuState *cpu, uint32_t pc) {
  const unsigned at = pc & 0xffff;
  const unsigned axis = at == 0xdec0 || at == 0xdecb ? 8 : 5;
  const unsigned a = MmxX2CoopCameraAxis(cpu->A, axis);
  if (a == cpu->A) return;
  cpu->A = (uint16_t)a;
  cpu->_flag_N = (a & 0x8000) != 0;
  cpu->_flag_Z = a == 0;
  cpu->P = (uint8_t)((cpu->P & ~0x82) | (cpu->_flag_N ? 0x80 : 0) | (cpu->_flag_Z ? 2 : 0));
}

/* $00:990E (LDA $09FF): X2's stage loop ends the stage when its player's
 * HP is gone (mode 6). While the other seat plays on, it takes the main role
 * first: the check reads its HP, and the fallen body plays its death out as
 * the other seat. */
static void main_fall_hook(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc;
  if (!s.active || s.pass || s.live != s.main || !gameplay() || g_ram[0xd2] != 4) return;
  const Seat *o = &s.seats[other()];
  if ((g_ram[PLAYER + 0x27] & 127) || o->status != SEAT_ALIVE || !(o->body[0x27] & 127) ||
      o->body[2] == ACTION_DEATH) return;
  hand_main(other());
  diag_event("main-fell", other());
}

/* Interpreter twin of the generated $08:94C3 hook. */
static void pass_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (MmxX2CoopPassEnd(cpu)) interp_bridge_pre_opcode_redirect(0x089428);
}

void MmxX2CoopRegisterHooks(void) {
  static bool done;
  if (done) return;
  done = true;
  interp_bridge_set_pre_opcode_hook_owner(1);
  (void)interp_bridge_add_pre_opcode_hook(0x0894c3, pass_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x08c9b8, chr_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x08c325, terrain_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x00990e, main_fall_hook);
  {
    static const uint32_t cameras[] = {0x00dea1, 0x00deac, 0x00dec0, 0x00decb};
    for (unsigned i = 0; i < sizeof(cameras) / sizeof(cameras[0]); ++i)
      (void)interp_bridge_add_pre_opcode_hook(cameras[i], camera_hook);
  }
  (void)interp_bridge_add_pre_opcode_hook(CONTACT_ENTRY, contact_hook);
  for (unsigned i = 0; i < sizeof(kContactReturns) / sizeof(kContactReturns[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kContactReturns[i], contact_hook);
  (void)interp_bridge_add_pre_opcode_hook(OVERLAP_ENTRY, overlap_hook);
  {
    /* The object loops' returns (interpreted): items $00:CFB6/CFE4, enemy
     * shots D16F/D19D, enemies D1D6/D204. */
    static const uint32_t done[] = {0x00cfb6, 0x00cfe4, 0x00d16f, 0x00d19d, 0x00d1d6, 0x00d204};
    for (unsigned i = 0; i < sizeof(done) / sizeof(done[0]); ++i)
      (void)interp_bridge_add_pre_opcode_hook(done[i], object_done_hook);
  }
  for (unsigned i = 0; i < sizeof(kOverlapReturns) / sizeof(kOverlapReturns[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kOverlapReturns[i], overlap_hook);
  for (unsigned i = 0; i < sizeof(kSolidEntries) / sizeof(kSolidEntries[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kSolidEntries[i], solid_hook);
  (void)interp_bridge_add_pre_opcode_hook(CARRY_ENTRY, carry_hook);
  for (unsigned i = 0; i < sizeof(kHeldStates) / sizeof(kHeldStates[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kHeldStates[i], held_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x058654, oil_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x08a628, beam_camera_hook);
  for (unsigned i = 0; i < sizeof(kSolidReturns) / sizeof(kSolidReturns[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kSolidReturns[i], solid_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
}

/* A seat's body (live or stored) while it plays, for shared items. */
uint8_t *MmxX2CoopSeatBody(unsigned seat) {
  if (!s.active || seat > 1 || s.seats[seat].status != SEAT_ALIVE) return NULL;
  return seat == s.live ? g_ram + PLAYER : s.seats[seat].body;
}
unsigned MmxX2CoopMainSeat(void) { return s.main; }
/* Where player 2's meter was last drawn (its badge), and whose it is. */
static int meter_x = -1, meter_y = -1;
static unsigned meter_seat;
bool MmxX2CoopMeterPos(int *x, int *y, unsigned *seat) {
  if (meter_x < 0) return false;
  *x = meter_x; *y = meter_y; *seat = meter_seat;
  return true;
}

/* The seat not in the player struct now, alive: its stored body. */
uint8_t *MmxX2CoopOtherBody(void) {
  if (!s.active) return NULL;
  const unsigned o = s.live ^ 1u;
  const Seat *p = &s.seats[o];
  if (p->status != SEAT_ALIVE || (p->body[2] == ACTION_DEATH && p->body[3] >= 4)) return NULL;
  return s.seats[o].body;
}

const uint8_t *MmxX2CoopDrawnSeat(MmxZeroState *state, unsigned *seat) {
  if (!s.active) return NULL;
  for (unsigned i = 0; i < 2; ++i) {
    if (s.seats[i].character != MMX_COOP_ZERO || i == s.live) continue;
    if (s.seats[i].status != SEAT_ALIVE) return NULL;
    /* Dying: gone once the orbs burst ($08:A68B, substate 4). */
    if (s.seats[i].body[2] == ACTION_DEATH && s.seats[i].body[3] >= 4) return NULL;
    if (state) *state = s.zero_owner == i ? MmxZeroGetState() : s.seats[i].zero;
    if (seat) *seat = i;
    return s.seats[i].body;
  }
  return NULL;
}

/* ── Player 2's meter (X1's coop_meter_row in X2's HUD tiles) ── */

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

static unsigned obj_pixel(unsigned tile, unsigned x, unsigned y) {
  const unsigned number = (tile + (y / 8) * 16 + x / 8) & 255;
  const uint16_t *t = g_ppu->vram + ((PPU_objTileAdr1(g_ppu) + number * 16 + (y & 7)) & 0x7fff);
  const unsigned bit = 7 - (x & 7);
  return ((t[0] >> bit) & 1) | ((t[0] >> (bit + 8)) & 1) << 1 |
      ((t[8] >> bit) & 1) << 2 | ((t[8] >> (bit + 8)) & 1) << 3;
}

/* A 16x16 HUD sprite: OAM-style y (screen row y + 1 is its first line). */
static void hud_sprite(int x, int y, unsigned tile, unsigned palette, bool zero_badge) {
  for (int row = 0; row < 16; ++row) {
    const int sy = y + row;
    if (sy < 0 || sy >= 224) continue;
    uint32_t *dst = screen_row(sy);
    for (int col = 0; col < 16; ++col) {
      const int sx = x + col;
      if (sx < -(int)g_ppu->extraLeftCur || sx >= 256 + (int)g_ppu->extraRightCur) continue;
      if (zero_badge) {
        const int color = MmxZeroHudColor((unsigned)col, (unsigned)row);
        if (color >= 0) { dst[sx] = screen_color((uint16_t)color); continue; }
      }
      const unsigned pixel = obj_pixel(tile, (unsigned)col, (unsigned)row);
      if (pixel) dst[sx] = screen_color(g_ppu->cgram[128 + palette * 16 + pixel]);
    }
  }
}

static void meter(int x, int icon_y, unsigned value, unsigned maximum, bool zero) {
  typedef struct { int y; unsigned tile; } Strip;
  Strip strips[8];
  unsigned count = 0;
  if (maximum > 32) maximum = 32;
  if (value > maximum) value = maximum;
  int top = 64, remaining = (int)value;
  while (remaining > 0 && count < 6) {
    remaining -= 8;
    const int sy = top - (remaining < 0 ? remaining * 2 : 0);
    strips[count++] = (Strip){sy, 0x80};
    top = sy - 16;
  }
  remaining = (int)maximum - (int)value;
  do {
    remaining -= 8;
    const int sy = top - (remaining < 0 ? remaining * 2 : 0);
    strips[count++] = (Strip){sy, 0x82};
    top = sy - 16;
  } while (remaining >= 0 && count < 7);
  strips[count++] = (Strip){top, 0x84};
  for (int i = (int)count - 1; i >= 0; --i)
    hud_sprite(x, strips[i].y + icon_y - 80, strips[i].tile, 2, false);
  hud_sprite(x, icon_y, 0x86, 2, zero);
}

void MmxX2CoopDrawHud(int badge_x, int badge_y) {
  meter_x = -1;
  /* Two cameras: each view shows its own player's meter. */
  if (MmxX2CoopIndependent()) return;
  if (!s.active || !g_ppu || !gameplay() || badge_x < 0) return;
  /* Widescreen moves player 1's meters out to the edge: so does this one. */
  badge_x -= X2WsHudLeftShift();
  /* The seat that is not main: its meter beside X2's own (the main's). */
  const Seat *p = &s.seats[other()];
  if (p->status == SEAT_ABSENT) return;
  const uint8_t *body = seat_body(other());
  const unsigned hp = p->status == SEAT_FALLEN ? 0 : body[0x27] & 127;
  unsigned tallest = g_ram[0x1fd1] > 32 ? 32u : g_ram[0x1fd1];
  if (tallest < 28) tallest = 28;
  /* Horizontal: beside player 1's meters. Compact: Zero's below X's. */
  const bool compact = MmxRendererCompactCoopHud() && p->character == MMX_COOP_ZERO;
  const int x = badge_x + (compact ? 0 : 32);
  const int y = compact ? badge_y + 36 + 2 * (int)tallest : badge_y;
  meter(x, y, hp, g_ram[0x1fd1] ? g_ram[0x1fd1] : 16, p->character == MMX_COOP_ZERO);
  meter_x = x; meter_y = y; meter_seat = other();
}

/* Side by side, player 2's pane: player 2's meter where player 1's is. */
void MmxX2CoopDrawPaneHud(int badge_x, int badge_y) {
  if (!s.active || !g_ppu || !gameplay() || badge_x < 0) return;
  const Seat *p = &s.seats[1];
  if (p->status == SEAT_ABSENT) return;
  const unsigned hp = p->status == SEAT_FALLEN ? 0 : seat_body(1)[0x27] & 127;
  badge_x -= X2WsHudLeftShift();
  meter(badge_x, badge_y, hp, g_ram[0x1fd1] ? g_ram[0x1fd1] : 16, p->character == MMX_COOP_ZERO);
}

/* Snapshots (rollback, rewind, save states): the seats and pass state as a
 * frame boundary leaves them. A snapshot from another co-op setup is ignored. */
size_t MmxX2CoopStateSize(void) { return sizeof(s); }
/* Player 2's camera is presentation (drawn frames only), never state. */
void MmxX2CoopSaveState(uint8_t *out) {
  X2CoopState copy;
  memcpy(&copy, &s, sizeof(copy)); /* padding included: equal states, equal bytes */
  copy.cam2_x = copy.cam2_y = 0;
  copy.cam2_valid = false;
  memcpy(out, &copy, sizeof(copy));
}
bool MmxX2CoopLoadState(const uint8_t *in, size_t size) {
  if (size != sizeof(s)) return false;
  bool active, configured;
  memcpy(&configured, in + offsetof(X2CoopState, configured), sizeof(configured));
  memcpy(&active, in + offsetof(X2CoopState, active), sizeof(active));
  if (configured != s.configured || active != s.active) return false;
  const int cam2_x = s.cam2_x, cam2_y = s.cam2_y;
  const bool cam2_valid = s.cam2_valid;
  memcpy(&s, in, sizeof(s));
  s.cam2_x = cam2_x; s.cam2_y = cam2_y; s.cam2_valid = cam2_valid;
  s.pass = s.contact = s.terrain = 0;
  s.ov = 0;
  solid.pass = 0;
  carry_frame = -1;
  if (s.active) settle();
  return true;
}
