/* Shared progress between the X1 and X2 pages (X-tensions: X2 game
 * extension xx0.0.2): Heart Tanks, Sub Tanks and extra lives.
 *
 * Each game keeps its own pickup flags, so an item taken on one page is still
 * there to take in the other game's stages; what the player carries is the
 * sum of both:
 *
 *   - Heart Tanks: max HP = 16 + 2 per heart taken in either game, up to the
 *     games' own maximum of 32 (eight hearts).
 *   - Sub Tanks: one slot per tank taken in either game, up to the four slots
 *     both pause menus have. Their energy carries over at each page turn.
 *   - Extra lives: one count, carried over at each page turn.
 *
 *              X1       X2      (X2 from its new-game table $00:E4D5 and its
 *   lives      $1F80    $1FB3    password decoder $00:F229..F2E6, X1's from
 *   tanks      $1F83..  $1FB6..  $00:E4FF and $00:F234..F2AC)
 *   tank flags $1F99 hi $1FD0 hi (which stages' tanks were taken)
 *   hearts     $1F9C    $1FD3    (which stages' hearts were taken)
 *   max HP     $1F9A    $1FD1
 *   HP         $0BCF    $09FF    (player +$27, bit 7 kept)
 *
 * A tank slot is bit 7 (owned) plus its energy in the low nibble ($0E full).
 * The other game's flags are recorded at each page turn (which netplay runs
 * on every peer alike), and the running game's max HP and tank slots are
 * kept to the sum from its own frame. */
#include "mmx_x2share.h"

#include <string.h>

#include "cpu_state.h"
#include "snes/interp_bridge.h"

extern uint8_t g_ram[0x20000];

typedef struct Layout {
  uint16_t lives, tanks, tank_flags, hearts, max_hp, hp;
  uint16_t sound_ring, sound_index;  /* the sound command ring ($80:88CD / $80:8552) */
  uint8_t refill_sound;              /* a tank's refill tick, one per HP */
  uint8_t hp_per_unit, tick_frames;  /* a tank unit's HP; frames per HP tick */
} Layout;
static const Layout kX1 = {0x1f80, 0x1f83, 0x1f99, 0x1f9c, 0x1f9a, 0x0bcf, 0x0b72, 0x0ba3, 0x0c, 1, 4};
static const Layout kX2 = {0x1fb3, 0x1fb6, 0x1fd0, 0x1fd3, 0x1fd1, 0x09ff, 0x09a2, 0x09d3, 0x15, 2, 2};
enum { TANKS = 4, MAX_HEARTS = 8 };

/* Boss weapons (X1 to X2 boss weapons, Zero's Special Moves on the X2
 * page): X1's eight weapon bytes $1F87..$1F98 (bit 6 of $1F88 + 2n: the
 * boss of weapon n beaten), and X2's eight beaten bosses ($1FBB + 2n bit 6,
 * X2's menu order), as each game was last left. */
static struct {
  bool x1_valid, x2_valid;
  uint8_t x1_weapons[18];
  uint8_t x2_bosses;
} w;

void MmxX2ShareRecordWeapons(const uint8_t *ram, bool x2) {
  if (x2) {
    w.x2_bosses = 0;
    for (unsigned i = 0; i < 8; ++i)
      if (ram[0x1fbb + 2 * i] & 0x40) w.x2_bosses |= (uint8_t)(1u << i);
    w.x2_valid = true;
  } else {
    memcpy(w.x1_weapons, ram + 0x1f87, sizeof(w.x1_weapons));
    w.x1_valid = true;
  }
}

bool MmxX2ShareX2BossBeaten(unsigned weapon) {
  return w.x2_valid && weapon >= 1 && weapon <= 8 && (w.x2_bosses >> (weapon - 1) & 1);
}

bool MmxX2ShareX1Weapons(uint8_t out[18]) {
  if (!w.x1_valid) return false;
  memcpy(out, w.x1_weapons, sizeof(w.x1_weapons));
  return true;
}

static struct {
  bool valid;
  /* Pickup flags of the game not running (hearts, tank flags in bits 4-7). */
  uint8_t other_hearts, other_tank_flags;
  uint8_t lives;
  uint8_t tank_levels[TANKS];
} s;

static unsigned bits(unsigned v) {
  unsigned n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}

static const Layout *layout(bool x2) { return x2 ? &kX2 : &kX1; }

static unsigned tank_count(const uint8_t *ram, const Layout *g) {
  const unsigned n = bits(ram[g->tank_flags] & 0xf0) + bits(s.other_tank_flags & 0xf0);
  return n > TANKS ? TANKS : n;
}

static unsigned max_hp(const uint8_t *ram, const Layout *g) {
  unsigned hearts = bits(ram[g->hearts]) + bits(s.other_hearts);
  if (hearts > MAX_HEARTS) hearts = MAX_HEARTS;
  return 16 + 2 * hearts;
}

/* -- Xtreme Sub Tanks -------------------------------------------------------
 * Tanks past the fourth (both games hold eight) upgrade the slots in turn:
 * the fifth turns slot 1 into an Xtreme tank, the sixth slot 2, up to all
 * four with eight. An Xtreme tank holds a whole health bar (32 HP) and gives
 * only what is needed: used with 1 HP missing, it spends 1 and keeps the
 * rest. The native tanks hold 14 units of 2 HP and drain whole.
 *
 * Its energy lives here (e[]), not in WRAM. The slot's WRAM nibble is a
 * proxy the native code works with, rewritten every frame:
 *   - HP missing: the units (2 HP each) that heal what is missing, up to the
 *     energy, so the native use itself refills exactly that, with its own
 *     timing and refill tick (one HP and one tick every two frames) while
 *     the tank drains on screen from the top;
 *   - HP full: 0 when empty, else 1..13 or 14 (full, 32 HP), so the native
 *     fill reaches it first and its units (2 HP each) move into e[].
 * After the use e[] is less what was given (an odd HP the native gave over
 * is taken back; past 15 units the rest follows, tick by tick), and every
 * slot is written back as it was (X2 re-sorts its tanks after a use). */
/* A use is over when HP and the slots have held still this long (the native
 * ticks every 2 / 4 frames); a normal tank's re-sort is waited out longer. */
enum { XT_CAPACITY = 32, XT_QUIET = 12, XT_USE_QUIET = 6 };
static struct {
  uint8_t count;             /* Xtreme slots, 0..4 (slots 0..count-1) */
  uint8_t e[TANKS];          /* energy, HP 0..32 */
  uint8_t proxy[TANKS];      /* the nibble last written */
  uint8_t last_raw[TANKS];   /* each slot's byte last frame */
  uint8_t last_hp;
  bool using, stopped, normal_use, resync;
  uint8_t slot, start_e, start_hp, quiet, pending;
  uint8_t shown_hp;           /* HP as of the last frame (for drawing) */
  uint8_t snap[TANKS];       /* the slots as they were before a use */
} xt;

static uint8_t proxy_of(unsigned e, unsigned hp, unsigned max, unsigned per) {
  if (!e) return 0;
  const unsigned missing = max > hp ? max - hp : 0;
  if (missing) {
    const unsigned need = e < missing ? e : missing;
    const unsigned p = (need + per - 1) / per;
    return (uint8_t)(p < 1 ? 1 : p > 15 ? 15 : p);
  }
  if (e >= XT_CAPACITY) return 0x0e;
  const unsigned p = (e + per - 1) / per;
  return (uint8_t)(p < 1 ? 1 : p > 13 ? 13 : p);
}

static unsigned xtreme_count(const uint8_t *ram, const Layout *g) {
  const unsigned n = bits(ram[g->tank_flags] & 0xf0) + bits(s.other_tank_flags & 0xf0);
  return n > TANKS ? (n - TANKS > TANKS ? TANKS : n - TANKS) : 0;
}

static void write_slot(uint8_t *ram, const Layout *g, unsigned i, uint8_t nibble) {
  uint8_t *tank = ram + g->tanks + i;
  if (*tank & 0x80) *tank = (uint8_t)(0x80 | nibble);
}

static void write_proxies(uint8_t *ram, const Layout *g) {
  for (unsigned i = 0; i < xt.count; ++i) {
    xt.proxy[i] = proxy_of(xt.e[i], ram[g->hp] & 127, ram[g->max_hp], g->hp_per_unit);
    write_slot(ram, g, i, xt.proxy[i]);
  }
}

static void remember(const uint8_t *ram, const Layout *g) {
  for (unsigned i = 0; i < TANKS; ++i) xt.last_raw[i] = ram[g->tanks + i];
  xt.last_hp = ram[g->hp] & 127;
  xt.shown_hp = xt.last_hp;
}

/* After a normal tank's use: the normal tanks' energies, wherever the native
 * sort left them (the Xtreme slots' proxies taken out once each), back in
 * their slots after the Xtreme ones, fullest first. */
static void settle_normal(uint8_t *ram, const Layout *g, unsigned owned) {
  uint8_t pool[TANKS];
  unsigned n = 0;
  bool taken[TANKS] = {false};
  for (unsigned i = 0; i < owned; ++i) {
    const uint8_t v = ram[g->tanks + i] & 0x0f;
    bool proxy = false;
    for (unsigned j = 0; j < xt.count && !proxy; ++j)
      if (!taken[j] && xt.proxy[j] == v) { taken[j] = true; proxy = true; }
    if (!proxy && n < TANKS) pool[n++] = v;
  }
  for (unsigned a = 0; a < n; ++a)
    for (unsigned b = a + 1; b < n; ++b)
      if (pool[b] > pool[a]) { const uint8_t t = pool[a]; pool[a] = pool[b]; pool[b] = t; }
  unsigned k = 0;
  for (unsigned i = xt.count; i < owned; ++i) write_slot(ram, g, i, k < n ? pool[k++] : 0);
}

static void xtreme_frame(uint8_t *ram, const Layout *g) {
  unsigned owned = 0;
  while (owned < TANKS && (ram[g->tanks + owned] & 0x80)) ++owned;
  const unsigned count = xtreme_count(ram, g);
  /* A slot just upgraded keeps what its tank held, as a share of a full
   * tank ($0E, 14 units): a full tank becomes a full Xtreme tank. */
  for (unsigned i = xt.count; i < count; ++i) {
    unsigned units = ram[g->tanks + i] & 0x0f;
    if (units > 14) units = 14;
    xt.e[i] = (uint8_t)((units * XT_CAPACITY + 7) / 14);
  }
  if (count != xt.count) { xt.count = (uint8_t)count; xt.resync = true; }
  const unsigned hp = ram[g->hp] & 127, max = ram[g->max_hp];
  if (!xt.count) { remember(ram, g); xt.using = xt.normal_use = false; return; }
  if (xt.resync) {
    xt.resync = xt.using = xt.normal_use = false;
    write_proxies(ram, g);
    remember(ram, g);
    return;
  }
  bool changed = hp != xt.last_hp;
  for (unsigned i = 0; i < TANKS; ++i) changed |= ram[g->tanks + i] != xt.last_raw[i];
  if (xt.using) {
    const unsigned i = xt.slot;
    if (xt.stopped) {
      /* Phase 2: the rest of what the native gave too little of, 1 HP every
       * second frame, as the native refill runs. */
      if (xt.pending && hp < max) {
        if (++xt.quiet % g->tick_frames == 0) {
          ram[g->hp] = (uint8_t)((ram[g->hp] & 128) | (hp + 1));
          --xt.pending;
          const unsigned k = ram[g->sound_index] & 0x1e;
          ram[g->sound_ring + k] = g->refill_sound;
          ram[g->sound_ring + k + 1] = 0;
          ram[g->sound_index] = (uint8_t)((k + 2) & 0x1e);
        }
        remember(ram, g);
        return;
      }
      xt.using = false;
      write_proxies(ram, g);
      remember(ram, g);
      return;
    }
    xt.quiet = changed ? 0 : (uint8_t)(xt.quiet + 1);
    if (xt.quiet >= XT_USE_QUIET) {
      /* The native use is over (it heals from its own copy of the proxy and
       * then re-sorts the slots): what the Xtreme tank gives is what was
       * missing, up to its energy. */
      const unsigned missing = max > xt.start_hp ? max - xt.start_hp : 0;
      const unsigned give = xt.start_e < missing ? xt.start_e : missing;
      const unsigned healed = hp > xt.start_hp ? hp - xt.start_hp : 0;
      xt.e[i] = (uint8_t)(xt.start_e - give);
      if (healed > give) ram[g->hp] = (uint8_t)((ram[g->hp] & 128) | (xt.start_hp + give));
      xt.pending = (uint8_t)(healed < give ? give - healed : 0);
      for (unsigned j = 0; j < TANKS; ++j)
        if (ram[g->tanks + j] & 0x80) ram[g->tanks + j] = (uint8_t)(0x80 | (xt.snap[j] & 0x0f));
      write_proxies(ram, g);
      xt.stopped = true;
      xt.quiet = 0;
    }
    remember(ram, g);
    return;
  }
  if (xt.normal_use) {
    xt.quiet = changed ? 0 : (uint8_t)(xt.quiet + 1);
    if (xt.quiet >= XT_QUIET) {
      xt.normal_use = false;
      settle_normal(ram, g, owned);
      write_proxies(ram, g);
    }
    remember(ram, g);
    return;
  }
  for (unsigned i = 0; i < xt.count; ++i) {
    const uint8_t cur = ram[g->tanks + i] & 0x0f;
    if (cur > xt.proxy[i]) {
      /* The native fill: 2 HP a unit. */
      const unsigned e = xt.e[i] + (unsigned)g->hp_per_unit * (cur - xt.proxy[i]);
      xt.e[i] = (uint8_t)(e > XT_CAPACITY ? XT_CAPACITY : e);
    } else if (cur < xt.proxy[i]) {
      /* The native use started on it. */
      xt.using = true;
      xt.stopped = false;
      xt.slot = (uint8_t)i;
      xt.start_e = xt.e[i];
      xt.start_hp = xt.last_hp;
      xt.quiet = 0;
      memcpy(xt.snap, xt.last_raw, sizeof(xt.snap));
      remember(ram, g);
      return;
    }
  }
  for (unsigned i = xt.count; i < owned; ++i)
    if ((ram[g->tanks + i] & 0x0f) < (xt.last_raw[i] & 0x0f)) {
      xt.normal_use = true;
      xt.quiet = 0;
    }
  write_proxies(ram, g);
  remember(ram, g);
}

void MmxX2ShareReset(void) {
  memset(&s, 0, sizeof(s));
  memset(&xt, 0, sizeof(xt));
}

void MmxX2ShareLeave(const uint8_t *ram, bool x2) {
  MmxX2ShareRecordWeapons(ram, x2);
  const Layout *g = layout(x2);
  s.other_hearts = ram[g->hearts];
  s.other_tank_flags = ram[g->tank_flags] & 0xf0;
  s.lives = ram[g->lives];
  for (unsigned i = 0; i < TANKS; ++i) s.tank_levels[i] = ram[g->tanks + i];
  s.valid = true;
}

/* The running game's max HP and tank slots from both games' pickups. */
static void enforce(uint8_t *ram, const Layout *g) {
  const unsigned max = max_hp(ram, g);
  if (ram[g->max_hp] != max) {
    ram[g->max_hp] = (uint8_t)max;
    if ((ram[g->hp] & 127) > max) ram[g->hp] = (uint8_t)((ram[g->hp] & 128) | max);
  }
  const unsigned count = tank_count(ram, g);
  for (unsigned i = 0; i < TANKS; ++i) {
    uint8_t *tank = ram + g->tanks + i;
    if (i < count && !(*tank & 128)) *tank = 128;  /* a tank from the other game, empty */
    else if (i >= count && (*tank & 128)) *tank = 0;
  }
}

void MmxX2ShareEnter(uint8_t *ram, bool x2) {
  if (!s.valid) return;
  const Layout *g = layout(x2);
  /* The page just left recorded its own flags; this page's are in `ram`. */
  ram[g->lives] = s.lives;
  for (unsigned i = 0; i < TANKS; ++i) ram[g->tanks + i] = s.tank_levels[i];
  /* Swap roles: the page just left is now the other one. Its flags stay in
   * s; nothing of this page's is recorded until it is left. */
  enforce(ram, g);
  xt.resync = true;
  xtreme_frame(ram, g);
}

size_t MmxX2ShareBlob(void *out, size_t cap) {
  const size_t size = sizeof(s) + sizeof(w) + sizeof(xt);
  if (out && cap >= size) {
    memcpy(out, &s, sizeof(s));
    memcpy((uint8_t *)out + sizeof(s), &w, sizeof(w));
    memcpy((uint8_t *)out + sizeof(s) + sizeof(w), &xt, sizeof(xt));
  }
  return size;
}

bool MmxX2ShareSetBlob(const void *in, size_t size) {
  if (!in || size != sizeof(s) + sizeof(w) + sizeof(xt)) return false;
  memcpy(&s, in, sizeof(s));
  memcpy(&w, (const uint8_t *)in + sizeof(s), sizeof(w));
  memcpy(&xt, (const uint8_t *)in + sizeof(s) + sizeof(w), sizeof(xt));
  xt.resync = true;
  return true;
}

/* A snapshot load (X2's bridge chunk): exactly as saved, mid-use included. */
bool MmxX2ShareLoadState(const void *in, size_t size) {
  if (!in || size != sizeof(s) + sizeof(w) + sizeof(xt)) return false;
  memcpy(&s, in, sizeof(s));
  memcpy(&w, (const uint8_t *)in + sizeof(s), sizeof(w));
  memcpy(&xt, (const uint8_t *)in + sizeof(s) + sizeof(w), sizeof(xt));
  return true;
}

void MmxX2ShareFrame(uint8_t *ram, bool x2) {
  const Layout *g = layout(x2);
  if (s.valid) enforce(ram, g);
  xtreme_frame(ram, g);
}

/* -- Xtreme Sub Tanks: drawing ---------------------------------------------
 * X4-X6's double tank, traced a pixel at a time from the reference
 * (docs/screenshots/x4-x6-sub-tanks-xtreme-style.jpg, 26x23 at about 10x),
 * with the final edits (a glass row more at the top and at the bottom).
 * Its glass ('g': one window in each tank, rows 5-11, and the strip of rows
 * 16-17 below the core) is black like the native tank's when
 * empty and fills from the bottom, both tanks together, in the native
 * tank's colours. */
static const char *const kXtremeArt[23] = {
  "....000000......000000....",
  "...04499440....04499440...",
  "..0000440000..0000440000..",
  "..0409999040..0409999040..",
  "..0000440000..0000440000..",
  "..44gggggg44..44gggggg44..",
  ".094gggggg490094gggggg490.",
  "0494gggggg404404gggggg4940",
  "0494gggggg400004gggggg4940",
  "0494gggg1104994011gggg4940",
  "0094ggg111100001111ggg4900",
  "0494gg11555799755511gg4940",
  "04000015510000001551000040",
  "04990917703888883771909940",
  "04440419708000000791404440",
  ".000000770888883077000000.",
  "..94gg15508000000551gg49..",
  "..44gg11503888883511gg44..",
  "..0099115100000015119900..",
  "..0400001115775111000040..",
  "...04944011000011044940...",
  "....000000049940000000....",
  "....0.0.0...00...0.0.0....",
};
enum { XT_W = 26, XT_H = 23 };

static uint16_t rgb(unsigned r, unsigned g, unsigned b) {
  return (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

static uint16_t art_color(char c) {
  /* The metal (0 2 4 6 9) in the native tank's own greys: black 24, 140
   * and 198 (traced shades 57 / 90 / 115 / 165 / 222 merged onto them). */
  static const uint8_t k[10][3] = {
    {24, 24, 24}, {49, 74, 148}, {24, 24, 24}, {198, 115, 74}, {140, 140, 140},
    {90, 148, 231}, {140, 140, 140}, {165, 206, 247}, {247, 247, 140}, {198, 198, 198},
  };
  if (c < '0' || c > '9') return 0;
  return rgb(k[c - '0'][0], k[c - '0'][1], k[c - '0'][2]);
}

/* The glass's nine rows (5-11, 16 and 17), each its own colour as the final
 * sprite has them: the native tank's seven bands (read from X2's full tank)
 * between a darker red at the bottom and a darker blue at the top. Empty
 * glass is the native tank's black. */
static const uint8_t kFill[9][3] = {
  {107, 0, 0}, {206, 49, 0}, {222, 132, 24}, {247, 214, 49}, {214, 222, 148},
  {181, 239, 255}, {123, 189, 247}, {74, 148, 247}, {49, 74, 148},
};
enum { XT_GLASS_BANDS = 9 };
static int glass_band(int row) {
  return row == 17 ? 0 : row == 16 ? 1 : row >= 5 && row <= 11 ? 13 - row : -1;
}

/* Rows from `clip_bottom` down are left off (the lower tanks stop where the
 * native tank does, above SUB.T). */
/* Unselected tanks are darker, as each game's menu darkens its own unselected
 * tanks with colour math: every 5-bit channel less `dim` (X1 8, X2 12). */
static uint16_t dimmed(uint16_t c, unsigned dim) {
  unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  r = r > dim ? r - dim : 0; g = g > dim ? g - dim : 0; b = b > dim ? b - dim : 0;
  return (uint16_t)(r | g << 5 | b << 10);
}

static void draw_xtreme(void (*put)(int x, int y, uint16_t c, void *ctx), void *ctx,
                        int x, int y, unsigned energy, int clip_bottom, unsigned dim) {
  unsigned filled = (energy * XT_GLASS_BANDS + XT_CAPACITY - 1) / XT_CAPACITY;
  if (filled > XT_GLASS_BANDS) filled = XT_GLASS_BANDS;
  for (int r = 0; r < XT_H && y + r < clip_bottom; ++r)
    for (int c = 0; c < XT_W; ++c) {
      const char a = kXtremeArt[r][c];
      if (a == '.') continue;
      uint16_t color;
      if (a == 'g') {
        const int band = glass_band(r);
        color = band >= 0 && (unsigned)band < filled
            ? rgb(kFill[band][0], kFill[band][1], kFill[band][2]) : rgb(24, 24, 24);
      } else {
        color = art_color(a);
      }
      put(x + c, y + r, dim ? dimmed(color, dim) : color, ctx);
    }
}

/* The pause screens' tank cells (16x16 background tiles), how far each
 * double tank moves (X1's cells are 24 px apart, so its columns move 1 px
 * apart; X2's lower tanks sit 1 px higher to leave SUB.T room), the menu
 * cursor's address and its value on the first tank (tanks in slot order),
 * and how much each game darkens an unselected tank. */
typedef struct PauseTanks {
  int x[TANKS], y[TANKS], dx[TANKS], dy[TANKS];
  uint16_t cursor;
  uint8_t first, step, dim;
} PauseTanks;
static const PauseTanks kX1Tanks = {{32, 56, 32, 56}, {142, 142, 166, 166},
                                    {-1, 1, -1, 1}, {0, 0, 0, 0}, 0x1ed2, 10, 1, 8};
static const PauseTanks kX2Tanks = {{136, 168, 136, 168}, {81, 81, 105, 105},
                                    {0, 0, 0, 0}, {0, 0, -1, -1}, 0x1ee2, 0x16, 2, 12};

/* The energy shown: during a use, what is left as the HP rises (the native
 * tank drains from the top while it heals), and while the rest is still
 * being given, that rest too. */
static unsigned shown_energy(unsigned i) {
  if (!xt.using || i != xt.slot) return xt.e[i];
  if (xt.stopped) return xt.e[i] + xt.pending;
  const unsigned hp = xt.shown_hp & 127;
  const unsigned healed = hp > xt.start_hp ? hp - xt.start_hp : 0;
  return xt.start_e > healed ? xt.start_e - healed : 0;
}

static void draw_pause(const PauseTanks *t, uint16_t panel,
                       void (*put)(int x, int y, uint16_t c, void *ctx), void *ctx) {
  for (unsigned i = 0; i < xt.count && i < TANKS; ++i)
    for (int r = 0; r < 16; ++r)
      for (int c = 0; c < 16; ++c) put(t->x[i] + c, t->y[i] + r, panel, ctx);
  const unsigned cursor = g_ram[t->cursor];
  for (unsigned i = 0; i < xt.count && i < TANKS; ++i) {
    const bool selected = cursor == t->first + i * t->step;
    draw_xtreme(put, ctx, t->x[i] + 8 - XT_W / 2 + t->dx[i], t->y[i] + 8 - XT_H / 2 - 1 + t->dy[i],
                shown_energy(i), 1 << 20, selected ? 0 : t->dim);
  }
}

typedef struct Target { uint32_t *pixels; int stride, left; const uint8_t *mult; } Target;

static void put_target(int x, int y, uint16_t c, void *ctx) {
  const Target *t = ctx;
  if (x < 0 || x >= 256 || y < 0 || y >= 224) return;
  const unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  uint32_t v;
  if (t->mult) v = (uint32_t)t->mult[r] << 16 | (uint32_t)t->mult[g] << 8 | t->mult[b];
  else v = (uint32_t)((r << 3) | (r >> 2)) << 16 | (uint32_t)((g << 3) | (g >> 2)) << 8 | ((b << 3) | (b >> 2));
  t->pixels[(ptrdiff_t)y * t->stride + t->left + x] = v;
}

/* X2's "SUB.T" (x 136-175) and "EXIT" (x 200-231) move
 * down 4 px under the lower double tanks (rows 101-122), text rows 121-127
 * to 125-131: two rows clear
 * above them and two above the bar (row 134). */
static void move_labels(uint32_t *top_row, int stride, uint32_t green) {
  static const int spans[2][2] = {{136, 175}, {200, 231}};
  enum { FROM = 121, ROWS = 7, DOWN = 4 };
  uint32_t saved[ROWS][40];
  for (unsigned k = 0; k < 2; ++k) {
    const int x0 = spans[k][0], w = spans[k][1] - x0 + 1;
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < w; ++c) {
        uint32_t *px = top_row + (ptrdiff_t)(FROM + r) * stride + x0 + c;
        saved[r][c] = *px;
        *px = green;
      }
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < w; ++c)
        if (saved[r][c] != green) top_row[(ptrdiff_t)(FROM + DOWN + r) * stride + x0 + c] = saved[r][c];
  }
}

void MmxX2ShareDrawX2Pause(uint32_t *top_row, int stride, const uint8_t *brightness) {
  if (!xt.count || !top_row) return;
  Target t = {top_row, stride, 0, brightness};
  const uint16_t panel = rgb(66, 173, 132);
  if (xt.count >= 3) {
    const unsigned r = panel & 31, g = (panel >> 5) & 31, b = (panel >> 10) & 31;
    move_labels(top_row, stride,
                (uint32_t)brightness[r] << 16 | (uint32_t)brightness[g] << 8 | brightness[b]);
  }
  draw_pause(&kX2Tanks, panel, put_target, &t);
}

bool MmxX2ShareX1PauseShown(const uint8_t *ram) {
  return xt.count && ram && ram[0xd1] == 2 && ram[0xd2] == 4 &&
      (ram[0x1f10] == 6 || ram[0x1f10] == 8) && (ram[0xc3] & 0x80);
}

void MmxX2ShareDrawX1Pause(uint32_t *pixels, int width, int left) {
  if (!pixels || !xt.count) return;
  Target t = {pixels, width, left, NULL};
  draw_pause(&kX1Tanks, rgb(49, 140, 66), put_target, &t);
}

void MmxX2ShareXtremeSave(uint8_t out[4]) { memcpy(out, xt.e, TANKS); }
void MmxX2ShareXtremeLoad(const uint8_t in[4]) {
  for (unsigned i = 0; i < TANKS; ++i) xt.e[i] = in[i] > XT_CAPACITY ? XT_CAPACITY : in[i];
  xt.resync = true;
  xt.using = xt.normal_use = false;
}

/* A Sub Tank pickup with all four slots owned ($01:DC15 X2, $81:E662 X1;
 * tools/apply_xtreme_tank_hooks.py): with both games' tanks counted, the
 * fifth to eighth are taken too. The pickup's flag (D + $0B) goes into this
 * game's tank flags and the pickup carries on as a free slot's would (its
 * chime and refill); xtreme_frame then upgrades the next slot. */
int MmxX2ShareTankPickup(CpuState *cpu, int x2) {
  const Layout *g = layout(x2 != 0);
  const uint8_t flag = g_ram[(uint16_t)(cpu->D + 0x0b)];
  uint8_t *flags = g_ram + g->tank_flags;
  if (!flag || (*flags & flag)) return 0;
  const unsigned total = bits(*flags & 0xf0) + bits(s.other_tank_flags & 0xf0);
  if (total >= 2 * TANKS) return 0;
  *flags |= flag;
  return 1;
}

/* The same blocks when they run interpreted. */
static void pickup_hook(CpuState *cpu, uint32_t pc) {
  const unsigned at = pc & 0xffff;
  const int x2 = at == 0xdc15;
  if (MmxX2ShareTankPickup(cpu, x2))
    interp_bridge_pre_opcode_redirect((pc & 0xff0000) | (x2 ? 0xdc1a : 0xe667));
}

void MmxX2ShareRegisterHooks(void) {
  static bool done;
  if (done) return;
  done = true;
  (void)interp_bridge_add_pre_opcode_hook(0x81e662, pickup_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x01e662, pickup_hook);
  interp_bridge_set_pre_opcode_hook_owner(1);
  (void)interp_bridge_add_pre_opcode_hook(0x01dc15, pickup_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x81dc15, pickup_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
}
