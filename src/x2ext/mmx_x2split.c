/* Side by side on the X2 page (Local co-op display: Side by side).
 *
 * X2 draws through the PPU with one camera, which follows player 1: that
 * field is player 1's pane. Player 2's pane is a second raster of the same
 * frame, from the PPU state the field started with, with player 2's camera:
 *
 *  - BG1 and BG2: their tilemaps hold only the region around player 1's
 *    camera, so the pane's region is rebuilt from the stage layout in RAM
 *    ($7E:E800 / $7E:EC00 screens, blocks at $7E:2800 / $7E:A600, the 8x8
 *    tile table at [$09C5] / [$09C8]; X1's renderer reads the same layout at
 *    X1's addresses). A layer whose tilemap does not match the layout at
 *    player 1's camera (a scene drawing its own tiles) is only scrolled.
 *    BG2's parallax follows the ratio X2 is moving it at.
 *  - Sprites: X2's own OAM, moved to player 2's camera. The co-op object
 *    window is widened around player 1's camera (X2's widescreen spawn
 *    margin), so objects in player 2's view are alive and in OAM. Player 1's
 *    meters are replaced by player 2's; Zero's seat is drawn for this camera.
 *  - HDMA runs as for the field; scroll values it writes get the pane's
 *    offset.
 *
 * Nothing here writes guest state: the pane is drawn into its own PPU copy,
 * and g_ppu / the DMA unit are put back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "mmx_netplay.h"
#if SNESRECOMP_NET
#include "netplay/snes_netplay.h"
#endif
#include "mmx_x2coop.h"
#include "mmx_x2zero.h"
#include "desktop/local_views.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include "snes/cart.h"
#include "snes/snes.h"

extern uint8_t g_ram[0x20000];
extern Snes *g_snes;

enum { PANE_W = 256, PANE_H = 224 };
/* A pane's width (Mega Man X Widescreen: wider than PANE_W) and its margin
 * on each side; PANE_W is the native view inside it. */
static int s_w = PANE_W, s_e;

static Ppu s_start;   /* the field's PPU state before its first line */
static Ppu s_pane;
static Dma s_dma;
static bool s_have_start, s_pane_ready;
static uint32_t s_pixels[kPpuBufWidth * PANE_H];

/* BG2 parallax: its unwrapped plane position and the rate it moves at. */
static struct {
  bool valid;
  int camera[2], plane[2];
  int anchor_camera[2], anchor_plane[2]; /* where the rate is measured from */
  int rate4[2]; /* quarters of the camera's movement */
} s_bg2;

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

bool MmxX2SplitWanted(void) { return MmxX2CoopSplit(); }

/* Netplay cameras: Independent, on player 2's peer: this peer shows player
 * 2's camera (the pane) instead of the field. */
static bool netplay_second_view(void) {
#if SNESRECOMP_NET
  return MmxNetplayActive() && MmxX2CoopIndependent() && snes_netplay_local_slot() == 1;
#else
  return false;
#endif
}

void MmxX2SplitFrameStart(void) {
  s_pane_ready = false;
  s_have_start = (MmxX2CoopSplit() || netplay_second_view()) && g_ppu;
  if (s_have_start) memcpy(&s_start, g_ppu, sizeof(Ppu));
}

/* ── The stage layout ─────────────────────────────────────────────────── */

static bool stage_tile(unsigned layer, int x, int y, uint16_t *entry) {
  if (x < 0 || y < 0 || x >= 8192 || y >= 8192 || !g_snes || !g_snes->cart) return false;
  const uint8_t *rom = g_snes->cart->rom;
  const size_t size = g_snes->cart->romSize;
  const unsigned layout = layer ? 0xec00 : 0xe800, screens = layer ? 0xa600 : 0x2800;
  const unsigned screen = g_ram[layout + (y >> 8) * 32 + (x >> 8)];
  const unsigned block = (screens + screen * 512 + ((y & 0xf0) << 1) + ((x & 0xf0) >> 3)) & 0xffff;
  const unsigned metatile = word(g_ram + block);
  const unsigned pointer = 0x9c5 + layer * 3;
  const unsigned table = word(g_ram + pointer) | (unsigned)g_ram[pointer + 2] << 16;
  const unsigned address = (table & 0xff0000) |
      ((table + metatile * 8 + ((y & 8) ? 4 : 0) + ((x & 8) ? 2 : 0)) & 0xffff);
  const size_t at = ((address >> 16) & 0x7f) * 0x8000u + (address & 0x7fff);
  if (!rom || at + 2 > size) return false;
  *entry = (uint16_t)(rom[at] | rom[at + 1] << 8);
  return true;
}

static unsigned map_address(const Ppu *p, unsigned layer, unsigned cx, unsigned cy) {
  const unsigned sc = p->bgXsc[layer];
  unsigned a = (sc & 0xfc) * 256 + (cx & 31) + (cy & 31) * 32;
  if ((sc & 1) && (cx & 32)) a += 1024;
  if ((sc & 2) && (cy & 32)) a += (sc & 1) ? 2048 : 1024;
  return a & 0x7fff;
}

/* The layer's tilemap is the layout at plane position (wx, wy). */
static bool layout_backed(const Ppu *p, unsigned layer, int wx, int wy) {
  if (PPU_bigTiles(p, layer)) return false;
  unsigned ok = 0, n = 0;
  for (int ty = 4; ty < PANE_H; ty += 24)
    for (int tx = 4; tx < PANE_W; tx += 24) {
      uint16_t e;
      if (!stage_tile(layer, wx + tx, wy + ty, &e)) continue;
      const unsigned px = (p->hScroll[layer] + (unsigned)tx) & 1023;
      const unsigned py = (p->vScroll[layer] + (unsigned)ty) & 1023;
      ++n;
      ok += p->vram[map_address(p, layer, px >> 3, py >> 3)] == e;
    }
  return n >= 24 && ok * 10 >= n * 9;
}

/* Write the layout's tiles for the view at plane position (wx, wy). */
static void rebuild(Ppu *p, unsigned layer, int wx, int wy) {
  const int x0 = wx & ~7, y0 = wy & ~7;
  for (int ty = 0; ty <= PANE_H / 8 + 1; ++ty)
    for (int tx = 0; tx <= PANE_W / 8 + 1; ++tx) {
      const int x = x0 + tx * 8, y = y0 + ty * 8;
      uint16_t e;
      if (!stage_tile(layer, x, y, &e)) continue;
      p->vram[map_address(p, layer, (unsigned)x >> 3, (unsigned)y >> 3)] = e;
    }
}

/* Unwrap a 10-bit scroll register near `near`. */
static int unwrap(unsigned reg, int near) {
  return near + ((int)((reg - (unsigned)near + 512) & 1023) - 512);
}

static void track_bg2(const Ppu *p, int cx, int cy) {
  const int camera[2] = {cx, cy};
  const unsigned reg[2] = {p->hScroll[1], p->vScroll[1]};
  if (!s_bg2.valid) {
    for (unsigned i = 0; i < 2; ++i) {
      s_bg2.camera[i] = camera[i];
      s_bg2.plane[i] = (int)(reg[i] & 1023);
      /* From where it stands (parallax planes start at the stage origin). */
      s_bg2.rate4[i] = camera[i] >= 64 ? (s_bg2.plane[i] * 4 + camera[i] / 2) / camera[i] : 2;
      if (s_bg2.rate4[i] < 0 || s_bg2.rate4[i] > 4) s_bg2.rate4[i] = 2;
      s_bg2.anchor_camera[i] = s_bg2.camera[i];
      s_bg2.anchor_plane[i] = s_bg2.plane[i];
    }
    s_bg2.valid = true;
    return;
  }
  for (unsigned i = 0; i < 2; ++i) {
    const int plane = unwrap(reg[i], s_bg2.plane[i]);
    const int step = camera[i] - s_bg2.camera[i];
    s_bg2.camera[i] = camera[i];
    s_bg2.plane[i] = plane;
    if (step > 64 || step < -64) {
      /* A jump (a door, a checkpoint): measure again from here. */
      s_bg2.anchor_camera[i] = camera[i];
      s_bg2.anchor_plane[i] = plane;
      continue;
    }
    /* Over 32 pixels of camera movement: the plane moves in whole pixels,
     * so a frame or two says little about its rate. */
    const int moved = camera[i] - s_bg2.anchor_camera[i];
    if (moved < 32 && moved > -32) continue;
    const int drawn = plane - s_bg2.anchor_plane[i];
    const int r = (drawn * 8 / moved + 1) / 2;
    if (r >= 0 && r <= 4) s_bg2.rate4[i] = r;
    s_bg2.anchor_camera[i] = camera[i];
    s_bg2.anchor_plane[i] = plane;
  }
}

/* ── Sprites ──────────────────────────────────────────────────────────── */

static bool hud_sprite(unsigned tile, unsigned attr) {
  return tile >= 0x80 && tile < 0x90 && (attr & 0x31) == 0x30;
}

static void hide(Ppu *p, unsigned i) {
  p->oam[i * 2] = (uint16_t)((p->oam[i * 2] & 0xff) | 0xe000);
}

/* X2's OAM is for player 1's camera; move it by (dx, dy). An entry's 9-bit
 * X is read on the side the pane looks toward (the widened object window
 * runs past both edges). Player 1's meters go; other HUD stays put. */
static void move_oam(Ppu *p, int dx, int dy) {
  static const uint8_t kSizes[8][2] = {
      {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}};
  const unsigned sizes = PPU_objSize(p) & 7;
  for (unsigned i = 0; i < 128; ++i) {
    const unsigned low = p->oam[i * 2], attr_tile = p->oam[i * 2 + 1];
    const unsigned shift = (i & 3) * 2, high = p->highOam[i >> 2] >> shift;
    const unsigned x9 = (low & 0xff) | (high & 1) << 8, y = low >> 8;
    const int size = kSizes[sizes][(high >> 1) & 1];
    if (hud_sprite(attr_tile & 0xff, attr_tile >> 8)) {
      if (x9 < 48) hide(p, i);
      continue;
    }
    int sy;
    if (y < PANE_H) sy = (int)y;
    else if ((int)y + size > 256) sy = (int)y - 256;
    else continue; /* parked */
    const int a = (int)x9 - dx, b = (int)x9 - 512 - dx;
    int nx;
    if (a > -size - s_e && a < PANE_W + s_e) nx = a;
    else if (b > -size - s_e && b < PANE_W + s_e) nx = b;
    else { hide(p, i); continue; }
    const int ny = sy - dy;
    if (ny <= -size || ny >= PANE_H) { hide(p, i); continue; }
    p->oam[i * 2] = (uint16_t)((nx & 0xff) | (ny & 0xff) << 8);
    p->highOam[i >> 2] = (uint8_t)((p->highOam[i >> 2] & ~(1u << shift)) |
                                   ((unsigned)(nx >> 8) & 1) << shift);
  }
}

/* ── Player 2's pane ──────────────────────────────────────────────────── */

void MmxX2SplitRenderPane(void) {
  if (!s_have_start) return;
  s_have_start = false;
  int c1x, c1y, c2x, c2y;
  if (!MmxX2CoopSplitCameras(&c1x, &c1y, &c2x, &c2y)) { s_bg2.valid = false; return; }
  track_bg2(&s_start, c1x, c1y);
  const int dx = c2x - c1x, dy = c2y - c1y;

  Ppu *const field = g_ppu;
  memcpy(&s_pane, &s_start, sizeof(Ppu));
  /* Host surfaces belong to the field. */
  s_pane.hdFrame = NULL;
  s_pane.hdCapture = NULL;
  s_pane.renderVram = NULL;
  memset(s_pane.overlayRenderBuffer, 0, sizeof(s_pane.overlayRenderBuffer));
  PpuSetWidescreenLineEnhancer(&s_pane, NULL, NULL);
  PpuSetExtraObjects(&s_pane, NULL, 0);
  PpuBeginDrawing(&s_pane, (uint8_t *)s_pixels, (size_t)s_w * 4, field->renderFlags);
  PpuSetExtraSpace(&s_pane, (uint8)s_e);

  /* Each layer's offset from the field's scroll. */
  int offset[2][2] = {{dx, dy}, {(dx * s_bg2.rate4[0]) / 4, (dy * s_bg2.rate4[1]) / 4}};
  for (unsigned layer = 0; layer < 2; ++layer) {
    if (!offset[layer][0] && !offset[layer][1]) continue;
    const int wx = layer == 0 ? unwrap(s_pane.hScroll[0], c1x) : s_bg2.plane[0];
    const int wy = layer == 0 ? unwrap(s_pane.vScroll[0], c1y) : s_bg2.plane[1];
    if (layout_backed(&s_pane, layer, wx, wy))
      rebuild(&s_pane, layer, wx + offset[layer][0], wy + offset[layer][1]);
  }
  move_oam(&s_pane, dx, dy);

  memcpy(&s_dma, g_dma, sizeof(Dma));
  g_ppu = &s_pane;
  MmxX2ZeroPaneObjects(c2x, c2y);
  SimpleHdma hdma[8];
  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int ch = 0; ch < 8; ch++) SimpleHdma_Init(&hdma[ch], &g_dma->channel[ch]);
  uint16_t set_h[2], set_v[2];
  for (unsigned layer = 0; layer < 2; ++layer) {
    s_pane.hScroll[layer] = (uint16_t)((s_pane.hScroll[layer] + offset[layer][0]) & 1023);
    s_pane.vScroll[layer] = (uint16_t)((s_pane.vScroll[layer] + offset[layer][1]) & 1023);
    set_h[layer] = s_pane.hScroll[layer];
    set_v[layer] = s_pane.vScroll[layer];
  }
  for (int line = 0; line <= PANE_H; line++) {
    /* A scroll HDMA wrote is the field's: give it this pane's offset. */
    for (unsigned layer = 0; layer < 2; ++layer) {
      if (s_pane.hScroll[layer] != set_h[layer])
        set_h[layer] = s_pane.hScroll[layer] =
            (uint16_t)((s_pane.hScroll[layer] + offset[layer][0]) & 1023);
      if (s_pane.vScroll[layer] != set_v[layer])
        set_v[layer] = s_pane.vScroll[layer] =
            (uint16_t)((s_pane.vScroll[layer] + offset[layer][1]) & 1023);
    }
    ppu_runLine(&s_pane, line);
    for (int ch = 0; ch < 8; ch++) SimpleHdma_DoLine(&hdma[ch]);
  }
  PpuSetExtraObjects(&s_pane, NULL, 0);
  int badge_x, badge_y;
  if (MmxX2ZeroCoopBadge(&badge_x, &badge_y)) MmxX2CoopDrawPaneHud(badge_x, badge_y);
  g_ppu = field;
  memcpy(g_dma, &s_dma, sizeof(Dma));
  s_pane_ready = true;
}

/* ── Presenter ────────────────────────────────────────────────────────── */

void MmxX2SplitPrepareFrame(int pane_w, int *frame_w, int *frame_h) {
  if (pane_w < PANE_W || pane_w > kPpuBufWidth) pane_w = PANE_W;
  s_w = pane_w & ~1;
  s_e = (s_w - PANE_W) / 2;
  *frame_w = MmxX2SplitWanted() ? 2 * s_w : s_w;
  *frame_h = PANE_H;
}

int MmxX2SplitDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field, int w, int h) {
  if (netplay_second_view()) {
    if (w != s_w || h != PANE_H || !s_pane_ready) return 0;
    for (int y = 0; y < PANE_H; ++y)
      memcpy(dst + (size_t)y * pitch, s_pixels + y * s_w, (size_t)s_w * 4);
    return 1;
  }
  if (!MmxX2SplitWanted() || w != 2 * s_w || h != PANE_H || !field) return 0;
  /* Outside a stage (menus, scenes, loading): the shared frame twice. */
  return SnesLocalViews_ComposeSideBySide(dst, pitch, s_w, PANE_H, field, (size_t)s_w * 4,
                                          s_pane_ready ? s_pixels : NULL, (size_t)s_w * 4) ? 1 : 0;
}
