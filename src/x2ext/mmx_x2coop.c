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
 * The camera follows player 1 (Unified); player 2 is held on screen and
 * brought back beside player 1 when left off it. Player 2's death plays out
 * without ending the stage ($08:A71C's INC $1F1C is undone), then player 2
 * returns beside player 1. */
#include "mmx_x2coop.h"

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
  /* Side by side: how far player 2's camera may be from player 1's. */
  LEASH_X = 128, LEASH_Y = 64,
};
enum { SEAT_ABSENT, SEAT_ALIVE, SEAT_FALLEN };

typedef struct Seat {
  uint8_t body[BODY];
  uint8_t helpers[HELPERS_SIZE];
  MmxZeroState zero;
  uint16_t pad, pressed;
  uint8_t character, status;
  uint16_t wait, away;
} Seat;

typedef struct X2CoopState {
  bool configured, active;
  Seat seats[2];
  /* Seat whose body is live at $09D8; seat whose Zero state is selected. */
  uint8_t live, zero_owner;
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
} X2CoopState;
static X2CoopState s;

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put_word(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

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
  const unsigned z = zero_seat();
  zero_select(z);
  MmxX2ZeroSetPlayerSource(z == s.live ? NULL : s.seats[z].body);
}

void MmxX2CoopPassBegin(void) {
  if (!s.active) return;
  zero_select(s.live);
  MmxX2ZeroSetPlayerSource(NULL);
}

bool MmxX2CoopLiveOwnsZero(void) { return !s.active || s.zero_owner == s.live; }
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
  zero_select(0);
  MmxX2ZeroSetPlayerSource(NULL);
  MmxCoopSetSeatOverride(false, 0, 0, 0);
  s.active = false;
}

/* X2's $00:E52E for a pad that is not port 1: held, previous and new
 * actions in the body (+$36/+$38/+$3A). */
static void apply_input(uint8_t *body, uint16_t input) {
  unsigned native = 0;
  for (unsigned bit = 0; bit < 12; ++bit)
    if (input & (1u << bit)) native |= 0x8000u >> bit;
  const unsigned buttons = ((native & 255) >> 2 & 0x3c) | (native >> 8 & 0xc0) | (native >> 12 & 3);
  static const uint16_t action_bits[6] = {0x4000, 0x8000, 0x0080, 0x0020, 0x0010, 0x1000};
  unsigned actions = native & 0x0f00;
  for (unsigned i = 0; i < 6; ++i) {
    const unsigned mask = g_ram[0xffc0 + i];
    if (mask && (buttons & mask) == mask) actions |= action_bits[i];
  }
  const unsigned previous = word(body + 0x36);
  put_word(body + 0x38, previous);
  put_word(body + 0x36, actions);
  put_word(body + 0x3a, actions & ~previous);
}

/* Player 1 stands in ordinary control: a place to bring player 2 in. */
static bool anchor_ready(const uint8_t *p1) {
  const unsigned action = p1[2];
  return p1[1] == 2 && (p1[0x27] & 127) && action <= 0x16 && action != ACTION_DEATH &&
      action != 0x0e && !g_ram[0x1f2b] && !g_ram[0x1f25];
}

/* Player 2 arrives beside player 1, from a copy of player 1's body. */
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
  diag_event("join", seat);
}

void MmxX2CoopFrameBegin(void) {
  if (!s.active) return;
  for (unsigned i = 0; i < 2; ++i) {
    const uint16_t pad = RtlGetPadState((int)i);
    s.seats[i].pressed = (uint16_t)(pad & ~s.seats[i].pad);
    s.seats[i].pad = pad;
  }
  MmxCoopSetSeatOverride(true, s.zero_owner, s.seats[s.zero_owner].pad, s.seats[s.zero_owner].pressed);
  s.contact = s.terrain = 0;
  if (!gameplay() || g_ram[0xd2] != 4) {
    /* Stage select, a stage's start or a restart: player 2 joins again. */
    if (s.seats[1].status != SEAT_ABSENT && debug_on())
      fprintf(stderr, "[x2coop] P2 leaves (mode %02X %02X %02X)\n", g_ram[0xd0], g_ram[0xd1], g_ram[0xd2]);
    s.seats[1].status = SEAT_ABSENT;
    return;
  }
  Seat *p2 = &s.seats[1];
  if (p2->status == SEAT_FALLEN && p2->wait) --p2->wait;
  if ((p2->status == SEAT_ABSENT || (p2->status == SEAT_FALLEN && !p2->wait)) &&
      anchor_ready(g_ram + PLAYER))
    join(1);
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
    Seat *p2 = &s.seats[1];
    if (s.live != 0) return 0;
    /* Player 1's door walks and the world's freezes hold player 2 too. */
    const unsigned a1 = g_ram[PLAYER + 2];
    if (p2->status != SEAT_ALIVE || !gameplay() || g_ram[0xd2] != 4 ||
        (a1 >= 0x18 && a1 <= 0x1e) || (g_ram[0x1f2b] && p2->body[2] != ACTION_DEATH)) {
      settle();
      return 0;
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
    memcpy(s.palette, g_ram + 0x300, sizeof(s.palette));
    body_select(1, true);
    zero_select(1);
    MmxX2ZeroSetPlayerSource(NULL);
    uint8_t *b = g_ram + PLAYER;
    apply_input(b, p2->pad);
    /* The NMI's joypad copies (held $A8, previous $AA, new $AC), which
     * Zero's code reads, as player 2's. */
    memcpy(s.joypad, g_ram + 0xa8, sizeof(s.joypad));
    {
      unsigned held = 0, fresh = 0;
      for (unsigned bit = 0; bit < 12; ++bit) {
        if (p2->pad & (1u << bit)) held |= 0x8000u >> bit;
        if (p2->pressed & (1u << bit)) fresh |= 0x8000u >> bit;
      }
      put_word(g_ram + 0xa8, held);
      put_word(g_ram + 0xaa, held & ~fresh);
      put_word(g_ram + 0xac, fresh);
    }
    /* Prepared outside the routine for player 1 ($00:D1F3 in X1): the
     * previous position and this frame's fire command. */
    memcpy(b + 0x22, b + 5, 2);
    memcpy(b + 0x24, b + 8, 2);
    g_ram[0x1f1e] = 0;
    s.pass = 2;
    return 1;
  }
  /* Player 2's pass ends. */
  uint8_t *b = g_ram + PLAYER;
  Seat *p2 = &s.seats[1];
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
    /* Player 2's death never restarts the stage. */
    g_ram[0x1f1c] = s.restart; g_ram[0x1f1f] = s.restart_f; g_ram[0x1f20] = s.restart_g;
    memcpy(g_ram + 0x300, s.palette, sizeof(s.palette));
    if (b[3] >= 6) {
      p2->status = SEAT_FALLEN;
      p2->wait = RESPAWN_FRAMES;
      if (debug_on()) fprintf(stderr, "[x2coop] P2 fell\n");
      diag_event("fell", 1);
    }
  } else {
    constrain(b, p2);
    if (p2->away >= AWAY_FRAMES) {
      /* Left behind off screen: back beside player 1. */
      p2->status = SEAT_ABSENT;
      if (debug_on()) fprintf(stderr, "[x2coop] P2 left behind; returning\n");
      diag_event("left-behind", 1);
    }
  }
  memcpy(g_ram + 0xa8, s.joypad, sizeof(s.joypad));
  body_select(0, true);
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
  if (s.terrain) {
    body_select(0, false);
    settle();
    s.terrain = 0;
    return 0;
  }
  if (s.live != 0 || s.seats[1].status != SEAT_ALIVE || !gameplay() || g_ram[0xd2] != 4) return 0;
  cpu->D = (uint16_t)(g_ram[(uint16_t)(cpu->S + 1)] | g_ram[(uint16_t)(cpu->S + 2)] << 8);
  cpu->S = (uint16_t)(cpu->S + 2);
  body_select(1, false);
  zero_select(1);
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
  if (!s.active || s.pass || s.contact || s.live != 0 ||
      s.seats[1].status != SEAT_ALIVE || !gameplay()) return;
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
    body_select(1, false);
    zero_select(1);
    MmxX2ZeroSetPlayerSource(NULL);
    s.contact = 2;
    cpu->P = s.contact_entry_p;
    cpu_p_to_mirrors(cpu);
    return 1;
  }
  const bool first_hit = (s.contact_a & 255) != 0, second_miss = !(cpu->A & 255);
  if (debug_on() && !second_miss)
    fprintf(stderr, "[x2coop] enemy %04X touches P2 (hp %u)\n", s.contact_d, g_ram[PLAYER + 0x27] & 127);
  if (!second_miss) diag_event("enemy-contact", 1);
  body_select(0, false);
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
static void chr_hook(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (s.active && s.pass == 2 && s.seats[s.live].character == MMX_COOP_ZERO &&
      s.seats[0].character == MMX_COOP_X) {
    (void)cpu;
    interp_bridge_pre_opcode_redirect(0x08c9b7); /* the RTL before it */
  }
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
  (void)interp_bridge_add_pre_opcode_hook(CONTACT_ENTRY, contact_hook);
  for (unsigned i = 0; i < sizeof(kContactReturns) / sizeof(kContactReturns[0]); ++i)
    (void)interp_bridge_add_pre_opcode_hook(kContactReturns[i], contact_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
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
  return (uint32_t *)(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch) +
      (g_ppu->extraLeftRight - g_ppu->extraLeftCur);
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
      if (sx < 0 || sx >= 256) continue;
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
  /* Two cameras: each view shows its own player's meter. */
  if (MmxX2CoopIndependent()) return;
  if (!s.active || !g_ppu || !gameplay() || badge_x < 0) return;
  const Seat *p = &s.seats[1];
  if (p->status == SEAT_ABSENT) return;
  const uint8_t *body = seat_body(1);
  const unsigned hp = p->status == SEAT_FALLEN ? 0 : body[0x27] & 127;
  unsigned tallest = g_ram[0x1fd1] > 32 ? 32u : g_ram[0x1fd1];
  if (tallest < 28) tallest = 28;
  /* Horizontal: beside player 1's meters. Compact: Zero's below X's. */
  const bool compact = MmxRendererCompactCoopHud() && p->character == MMX_COOP_ZERO;
  const int x = badge_x + (compact ? 0 : 32);
  const int y = compact ? badge_y + 36 + 2 * (int)tallest : badge_y;
  meter(x, y, hp, g_ram[0x1fd1] ? g_ram[0x1fd1] : 16, p->character == MMX_COOP_ZERO);
}

/* Side by side, player 2's pane: player 2's meter where player 1's is. */
void MmxX2CoopDrawPaneHud(int badge_x, int badge_y) {
  if (!s.active || !g_ppu || !gameplay() || badge_x < 0) return;
  const Seat *p = &s.seats[1];
  if (p->status == SEAT_ABSENT) return;
  const unsigned hp = p->status == SEAT_FALLEN ? 0 : seat_body(1)[0x27] & 127;
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
  if (s.active) settle();
  return true;
}
