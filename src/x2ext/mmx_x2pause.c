/* X1 to X2 boss weapons (X-tensions, under the X2 game extension).
 *
 *  - X2's pause screen gets a second page: L / R (PREV / NEXT WEAPON) turn
 *    between X2's weapons and an X1 weapons page in X2's pause layout, for X
 *    and for Zero alike. The X1 page's slots are blank for now (its weapons
 *    come later): the left panel below X.BUSTER is cleared, and the menu's
 *    cursor and choice buttons are held back so nothing hidden is chosen;
 *    Start still leaves the pause. A small tag names the page.
 *  - X1's X2 weapons page (X2 boss weapons) offers an X2 weapon only once its
 *    boss has been beaten on the X2 page (X2's $1FBB + 2n bit 6, recorded at
 *    each page turn; mmx_x2share.c).
 *
 *  - The page is remembered: the pause opens on it again, and in play L / R
 *    work on that page only: X2's weapons on the X2 page (X2's own cycle;
 *    Zero's Rakuhouha is off there), the X1 page's on the X1 page (X keeps
 *    the buster, X2's cycle never sees L / R; Zero's L / R toggle
 *    Rakuhouha). Start on one of Zero's skill rows chooses it, from the
 *    buster's place, so X2 equips nothing hidden there.
 * X2's pause is shown while $1F21 is 6 or 8 with $C4 bit 7 set (X1's own test,
 * $1F10 / $C3, at X2's addresses). The page and the last L / R are part of X2
 * snapshots (the bridge chunk), since they change which buttons X2 sees. */
#include "mmx_x2pause.h"

#include <string.h>

#include "mmx_weapons.h"
#include "mmx_x2share.h"
#include "../mmx_escape.h"
#include "mmx_x2zero.h"
#include "saber/mmx_saber_ksx.h"
#include "saber/mmx_saber_ksx_icons.h"
#include "mmx_x2ext.h"
#include "mods/mmx_mod_constructor.h"
#include "snes/ppu.h"

extern uint8_t g_ram[0x20000];
extern Ppu *g_ppu;

enum {
  PAD_B = 1u << 0, PAD_Y = 1u << 1, PAD_UP = 1u << 4, PAD_DOWN = 1u << 5,
  PAD_LEFT = 1u << 6, PAD_RIGHT = 1u << 7, PAD_A = 1u << 8, PAD_X = 1u << 9,
  PAD_L = 1u << 10, PAD_R = 1u << 11,
  /* The left panel's free rows below X.BUSTER (measured on X2's screen). */
  PANEL_LEFT = 18, PANEL_RIGHT = 124, PANEL_TOP = 33, PANEL_BOTTOM = 204,
};

static bool s_enabled;
static uint8_t s_page;      /* 0 X2's weapons, 1 X1's */
static uint8_t s_previous;  /* last frame's L / R */

bool MmxX2PauseEnabled(void) { return s_enabled; }

static bool paused(void) {
  return (g_ram[0x1f21] == 6 || g_ram[0x1f21] == 8) && (g_ram[0xc4] & 0x80);
}

/* X2's pause cursor ($1EE2) is twice the item: 0 X.BUSTER, 2..16 the
 * weapons on 16 px rows from y 17, $16 and on the Sub Tanks and EXIT. */
enum { CURSOR = 0x1ee2, CURSOR_RIGHT = 0x16, ROW_TOP = 17, ROW_PITCH = 16 };

static bool zero_skills_shown(void);
static bool skill_learned(const uint8_t x1[18], unsigned weapon);

/* The X1 page's left column: X.BUSTER, then (KSX Zero) each learned skill's
 * row. Returns the count; rows are cursor values. */
static unsigned left_rows(uint8_t rows[9]) {
  unsigned n = 0;
  rows[n++] = 0;
  uint8_t x1[18];
  if (zero_skills_shown() && MmxX2ShareX1Weapons(x1))
    for (unsigned weapon = 1; weapon <= 8; ++weapon)
      if (skill_learned(x1, weapon)) rows[n++] = (uint8_t)(weapon * 2);
  return n;
}

enum { PAD_START = 1u << 3, KSX_GIGA_ROW = 2 * 2 /* X1 weapon 2 */ };

bool MmxX2PauseKsxShoulders(void) {
  return !s_enabled || !MmxX2ExtInX2() || s_page == 1;
}

/* Zero's X2 weapons: none for now (his X2 page skills come later). While
 * X2's menu is up for him, its weapons' owned bits ($1FB9 + 2n bit 6, n 1..10:
 * the eight boss weapons, then $12 and $14) are taken away, so its X2 page
 * shows and offers none of them; they are put back as it closes (or as it
 * becomes X's). Which ones are away is part of X2 snapshots (bridge chunk). */
static uint16_t s_zero_hidden;
uint16_t MmxX2PauseZeroHidden(void) { return s_zero_hidden; }
void MmxX2PauseSetZeroHidden(uint16_t hidden) { s_zero_hidden = hidden & 0x3ff; }
static void zero_weapons(void) {
  const bool hide = g_ram[0xd0] == 2 && g_ram[0xd1] == 4 && g_ram[0x1f21] &&
      MmxX2ZeroPausedIsZero();
  for (unsigned n = 1; n <= 10; ++n) {
    uint8_t *slot = g_ram + 0x1fb9 + 2 * n;
    const uint16_t bit = (uint16_t)(1u << (n - 1));
    if (hide && (*slot & 0x40)) {
      *slot &= (uint8_t)~0x40;
      s_zero_hidden |= bit;
    } else if (!hide && (s_zero_hidden & bit)) {
      *slot |= 0x40;
      s_zero_hidden &= (uint16_t)~bit;
    }
  }
}

void MmxX2PauseFrameBegin(uint32_t *inputs) {
  zero_weapons();
  if (!s_enabled || !inputs) return;
  if (!paused()) {
    s_previous = 0;
    /* The X1 page in play: X keeps the buster (X2's cycle never sees L /
     * R); Zero's L / R reach KSX through its own input. */
    if (s_page && g_ram[0xd0] == 2 && g_ram[0xd1] == 4 && !MmxX2ZeroPausedIsZero())
      *inputs &= ~(uint32_t)(PAD_L | PAD_R);
    return;
  }
  /* Start leaves the pause on this page. */
  if ((*inputs & PAD_START) && g_ram[CURSOR] < CURSOR_RIGHT) {
    if (!s_page) MmxSaberKsxSetRakuhouha(false);
    else {
      MmxSaberKsxSetRakuhouha(g_ram[CURSOR] == KSX_GIGA_ROW);
      /* A skill row is no X2 weapon: leave from the buster's place. */
      g_ram[CURSOR] = 0;
      g_ram[0x1eda] = 0;
    }
  }
  const uint8_t lr = (uint8_t)((*inputs >> 10) & 3);
  const uint8_t ud = (uint8_t)((*inputs >> 4) & 3);
  const uint8_t ud_pressed = (uint8_t)(ud & ~(s_previous >> 2));
  if (lr & ~s_previous & 3) s_page ^= 1;
  s_previous = (uint8_t)(lr | ud << 2);
  /* L / R are the page turn here. */
  uint32_t held = PAD_L | PAD_R;
  if (s_page) {
    /* The X1 page: the Sub Tanks and EXIT are X2's own; the left column
     * is this page's (X.BUSTER and Zero's learned skills). Up / Down step
     * through it; a skill row takes no choice (a skill is an input, not a
     * weapon), X.BUSTER's does. */
    uint8_t rows[9];
    const unsigned n = left_rows(rows);
    uint8_t cur = g_ram[CURSOR];
    if (cur < CURSOR_RIGHT) {
      unsigned at = 0;
      for (unsigned i = 0; i < n; ++i) if (rows[i] <= cur) at = i;
      if (ud_pressed & 1) at = (at + n - 1) % n;       /* Up */
      else if (ud_pressed & 2) at = (at + 1) % n;      /* Down */
      if (rows[at] != cur && (ud_pressed & 3)) {
        /* X2's own cursor sound ($1C), queued in its ring ($09A2, index
         * $09D3) as its menu does. */
        const unsigned i = g_ram[0x9d3] & 0x1e;
        g_ram[0x9a2 + i] = 0x1c;
        g_ram[0x9a3 + i] = 0;
        g_ram[0x9d3] = (uint8_t)((i + 2) & 0x1e);
      }
      cur = rows[at];
      g_ram[CURSOR] = cur;
      held |= PAD_UP | PAD_DOWN;
      if (cur) held |= PAD_A | PAD_B | PAD_X | PAD_Y;
    }
  }
  *inputs &= ~held;
}

uint16_t MmxX2PauseState(void) { return (uint16_t)(s_page | s_previous << 8); }
void MmxX2PauseSetState(uint16_t state) {
  s_page = (uint8_t)(state & 1);
  s_previous = (uint8_t)((state >> 8) & 15);
}

/* ── Drawing over the finished frame ── */

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

static uint16_t bgr555(unsigned r, unsigned g, unsigned b) {
  return (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

/* 7x7 glyphs in the menu's own colors: yellow text, dark shadow. */
static const uint8_t kGlyphX[7] = {0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41};
static const uint8_t kGlyph1[7] = {0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1c};
static const uint8_t kGlyph2[7] = {0x3e, 0x41, 0x01, 0x1e, 0x20, 0x40, 0x7f};

static void glyph(const uint8_t rows[7], int x, int y, uint32_t text, uint32_t shadow) {
  for (int pass = 0; pass < 2; ++pass)
    for (int r = 0; r < 7; ++r)
      for (int c = 0; c < 7; ++c) {
        if (!(rows[r] & (0x40 >> c))) continue;
        const int sx = x + c + (pass ? 0 : 1), sy = y + r + (pass ? 0 : 1);
        if (sx < 0 || sx >= 256 || sy < 0 || sy >= 224) continue;
        screen_row(sy)[sx] = pass ? text : shadow;
      }
}

/* Zero's X1 page: KSX Zero's skills in X1's menu order (X1's left column
 * top to bottom, then its right one; here one column), each in its
 * Maverick's slot once that Maverick is beaten in X1 (X1 $1F88 + 2n bit 6,
 * as X1 last had it). Names in X2's own menu font (BG1 tiles: A-N $72..$7F,
 * O P $82 $83, R-Y $84..$8B, '.' $8C; palette 2, X.BUSTER's yellow), icons
 * in X1's icon frame. */
enum { SKILL_ICON_X = 24, SKILL_NAME_X = 48 };

/* X2's font has no Z: one drawn in its letters' shading (rows d c b b c d,
 * shadow e). */
static const char kZ[8][9] = {
  "0ddddde0", "0eeecce0", "000bbee0", "00bbee00",
  "0cce0000", "0ddddde0", "0eeeeee0", "00000000",
};

static int font_tile(char c) {
  if (c >= 'A' && c <= 'N') return 0x72 + (c - 'A');
  if (c == 'O' || c == 'P') return 0x82 + (c - 'O');
  if (c >= 'R' && c <= 'Y') return 0x84 + (c - 'R');
  if (c == '.') return 0x8c;
  return -1;
}

static void font_glyph(char c, int x, int y) {
  const uint16_t *cg = g_ppu->cgram + 32;
  if (c == 'Z') {
    for (int r = 0; r < 8; ++r)
      for (int col = 0; col < 8; ++col) {
        const char k = kZ[r][col];
        const unsigned index = k >= 'a' ? (unsigned)(k - 'a' + 10) : (unsigned)(k - '0');
        if (index && x + col < 256 && y + r < 224) screen_row(y + r)[x + col] = screen_color(cg[index]);
      }
    return;
  }
  const int tile = font_tile(c);
  if (tile < 0) return;
  const unsigned base = (PPU_bgTileAdr(g_ppu, 0) + (unsigned)tile * 16) & 0x7fff;
  for (int r = 0; r < 8; ++r) {
    const uint16_t lo = g_ppu->vram[(base + r) & 0x7fff], hi = g_ppu->vram[(base + 8 + r) & 0x7fff];
    for (int col = 0; col < 8; ++col) {
      const unsigned b = 7 - col;
      const unsigned index = ((lo >> b) & 1) | ((lo >> (b + 8)) & 1) << 1 |
          ((hi >> b) & 1) << 2 | ((hi >> (b + 8)) & 1) << 3;
      if (index && x + col < 256 && y + r < 224) screen_row(y + r)[x + col] = screen_color(cg[index]);
    }
  }
}

static void skill_icon(const uint16_t *icon, int x, int y) {
  for (int iy = 0; iy < 16; ++iy)
    for (int ix = 0; ix < 16; ++ix) {
      uint16_t c;
      if ((ix == 0 || ix == 15) && (iy == 0 || iy == 15)) continue;
      if (ix == 0 || ix == 15 || iy == 0 || iy == 15) c = 0x0c63;
      else if (ix == 1 || ix == 14 || iy == 1 || iy == 14) c = 0x7fff;
      else {
        c = icon[(iy - 2) * 12 + ix - 2];
        if (c & 0x8000) c = 0x0c63;
      }
      screen_row(y + iy)[x + ix] = screen_color(c);
    }
}

static bool zero_skills_shown(void) {
  return MmxSaberKsxEnabled() && MmxX2ZeroPausedIsZero();
}

/* X1's weapon n is $1F86 + 2n, here x1[2n - 1] ($1F87 on). */
static bool skill_learned(const uint8_t x1[18], unsigned weapon) {
  return (x1[2 * weapon - 1] & 0x40) && MmxSaberKsxSkillForWeapon(weapon) < MMX_KSX_SKILL_COUNT;
}

/* Rows not under the cursor are dimmed, as X2 dims its other weapons. */
static void dim_rect(int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1 && y < 224; ++y) {
    uint32_t *row = screen_row(y);
    for (int x = x0; x <= x1 && x < 256; ++x) {
      const uint32_t c = row[x];
      row[x] = ((c >> 16 & 255) * 3 / 4) << 16 | ((c >> 8 & 255) * 3 / 4) << 8 | (c & 255) * 3 / 4;
    }
  }
}

static void zero_skills(void) {
  uint8_t x1[18];
  if (!zero_skills_shown() || !MmxX2ShareX1Weapons(x1)) return;
  const uint32_t green = screen_color(bgr555(66, 173, 132));
  const uint8_t cursor = g_ram[CURSOR];
  for (unsigned weapon = 1; weapon <= 8; ++weapon) {
    /* Each skill on its Maverick's row (X1's order, one column), X2's
     * spacing: rows 9 and 10 stay free below the eighth. */
    if (!skill_learned(x1, weapon)) continue;
    const MmxKsxSkill skill = MmxSaberKsxSkillForWeapon(weapon);
    const char *name = MmxSaberKsxSkillName(skill);
    const int y = ROW_TOP + (int)weapon * ROW_PITCH;
    skill_icon(kMmxSaberKsxIcons[skill], SKILL_ICON_X, y);
    const size_t n = name ? strlen(name) : 0;
    /* 8 px letters where the name fits, else 7 (no blank column), moved
     * left toward the icon as far as HYOURETSUZAN needs. */
    const int advance = SKILL_NAME_X + (int)n * 8 <= PANEL_RIGHT + 1 ? 8 : 7;
    int left = SKILL_NAME_X;
    if (left + (int)n * advance > PANEL_RIGHT + 1) left = PANEL_RIGHT + 1 - (int)n * advance;
    if (left < SKILL_ICON_X + 17) left = SKILL_ICON_X + 17;
    for (size_t i = 0; i < n; ++i) font_glyph(name[i], left + (int)i * advance, y + 1);
    if (cursor != weapon * 2) {
      dim_rect(SKILL_ICON_X, y, PANEL_RIGHT, y + 15);
      /* The panel's own green stays as it is. */
      for (int yy = y; yy <= y + 15; ++yy) {
        uint32_t *row = screen_row(yy);
        const uint32_t dimmed = ((green >> 16 & 255) * 3 / 4) << 16 | ((green >> 8 & 255) * 3 / 4) << 8 | (green & 255) * 3 / 4;
        for (int x = SKILL_ICON_X; x <= PANEL_RIGHT; ++x) if (row[x] == dimmed) row[x] = green;
      }
    }
  }
}

static void after_draw(void);
void MmxX2PauseAfterDraw(void) {
  if (!g_ppu) return;
  if (paused()) after_draw();
  /* Heart Tanks past eight: blue health over the HP bar. */
  MmxX2ShareDrawX2Blue(screen_row(0), (int)(g_ppu->renderPitch / 4));
  /* Escape Bugs: EXIT's command list, or Start + Select's progress. */
  MmxEscapeDrawX2(screen_row(0), (int)(g_ppu->renderPitch / 4), g_ppu->brightnessMult);
}
static void after_draw(void) {
  /* X2 game extension: Xtreme Sub Tanks, whichever mods are on. */
  MmxX2ShareDrawX2Pause(screen_row(0), (int)(g_ppu->renderPitch / 4), g_ppu->brightnessMult);
  if (!s_enabled) return;
  if (s_page) {
    /* The X1 page: its slots are blank for now. */
    const uint32_t green = screen_color(bgr555(66, 173, 132));
    for (int y = PANEL_TOP; y <= PANEL_BOTTOM; ++y) {
      uint32_t *row = screen_row(y);
      for (int x = PANEL_LEFT; x <= PANEL_RIGHT; ++x) row[x] = green;
    }
    /* Zero's: his X1 skills (X's slots stay blank for now). */
    zero_skills();
  }
  /* The page's name at the panel's lower right. */
  const uint32_t text = screen_color(bgr555(247, 214, 49)), shadow = screen_color(bgr555(33, 8, 8));
  glyph(kGlyphX, PANEL_RIGHT - 18, PANEL_BOTTOM - 10, text, shadow);
  glyph(s_page ? kGlyph1 : kGlyph2, PANEL_RIGHT - 9, PANEL_BOTTOM - 10, text, shadow);
}

/* ── X1's X2 weapons page ── */

static bool weapon_owned(unsigned page, unsigned weapon) {
  return page != 1 || MmxX2ShareX2BossBeaten(weapon);
}

static void reset(void) {
  s_enabled = false;
  s_zero_hidden = 0;
  s_page = s_previous = 0;
  MmxWeaponsSetOwnedQuery(NULL);
}

static void activate(void) {
  s_enabled = true;
  MmxWeaponsSetOwnedQuery(weapon_owned);
}

SNES_MOD_CONSTRUCTOR(mmx_register_x2pause_plugin) {
  (void)snes_mod_register_reset_callback(reset);
  (void)snes_mod_register_activation_plugin("megaman-x.xtension.x1-x2-weapons", activate);
}
