/* On-screen gamepad: input source 3 (Android, Switch). A virtual controller
 * drawn over the game and driven by touch. Its buttons go through
 * HandleGamepadInput as seat 1's controller, so [GamepadMap] binds them
 * exactly like a real pad's: the D-pad and left stick are the pad's d-pad, the
 * right stick its four right-stick buttons, L2/R2/L3/R3 its extra controls. A
 * menu button at the top opens the in-game launcher. Included by host_main.c:
 * SDL3 (Android), or SDL2 on Switch. */
#if SNESRECOMP_SDL3 || defined(__SWITCH__)

/* ---- SDL2/SDL3 differences ------------------------------------------------ */
#if SNESRECOMP_SDL3
typedef SDL_FColor TouchColor;
#define TOUCH_FINGER_DOWN     SDL_EVENT_FINGER_DOWN
#define TOUCH_FINGER_UP       SDL_EVENT_FINGER_UP
#define TOUCH_FINGER_MOTION   SDL_EVENT_FINGER_MOTION
#define TOUCH_FINGER_CANCELED SDL_EVENT_FINGER_CANCELED
#define TOUCH_FINGER_ID(e)    ((e)->tfinger.fingerID)
static SDL_FColor TouchVertexColor(TouchColor c) { return c; }
static void TouchDrawColor(SDL_Renderer *r, TouchColor c) {
  SDL_SetRenderDrawColorFloat(r, c.r, c.g, c.b, c.a);
}
#define TouchFillRects(r, rects, n) SDL_RenderFillRects(r, rects, n)
#else
typedef struct TouchColor { float r, g, b, a; } TouchColor;
#define TOUCH_FINGER_DOWN     SDL_FINGERDOWN
#define TOUCH_FINGER_UP       SDL_FINGERUP
#define TOUCH_FINGER_MOTION   SDL_FINGERMOTION
#define TOUCH_FINGER_CANCELED 0xffffffffu   /* SDL2 has none */
#define TOUCH_FINGER_ID(e)    ((e)->tfinger.fingerId)
static Uint8 TouchByte(float v) { return (Uint8)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); }
static SDL_Color TouchVertexColor(TouchColor c) {
  return (SDL_Color){ TouchByte(c.r), TouchByte(c.g), TouchByte(c.b), TouchByte(c.a) };
}
static void TouchDrawColor(SDL_Renderer *r, TouchColor c) {
  SDL_SetRenderDrawColor(r, TouchByte(c.r), TouchByte(c.g), TouchByte(c.b), TouchByte(c.a));
}
#define TouchFillRects(r, rects, n) SDL_RenderFillRectsF(r, rects, n)
#endif

enum {
  kTouchDpad, kTouchA, kTouchB, kTouchX, kTouchY, kTouchL, kTouchR, kTouchL2, kTouchR2,
  kTouchL3, kTouchR3, kTouchStart, kTouchSelect, kTouchLStick, kTouchRStick, kTouchControls,
  kTouchMenu = kTouchControls, kTouchNone = -1,
};
/* The controller button behind each simple button (D-pad and sticks are
 * handled separately). The face buttons sit in SNES/Nintendo positions (A
 * right, B bottom, X top, Y left), and a pad button is named by where it is on
 * an SDL pad (A bottom, B right, X left, Y top), the same positional sense the
 * default [GamepadMap] and the launcher's binding grid use. */
static const int kTouchPadButton[kTouchControls] = {
  -1, kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
  kGamepadBtn_L2, kGamepadBtn_R2, kGamepadBtn_L3, kGamepadBtn_R3, kGamepadBtn_Start,
  kGamepadBtn_Back, -1, -1,
};
static const char *const kTouchLabel[kTouchControls] = {
  "", "A", "B", "X", "Y", "L", "R", "L2", "R2", "L3", "R3", "START", "SELECT", "", "",
};

typedef struct TouchShape {
  float x, y, r;        /* centre and radius (circles) */
  float w, h;           /* half extents for pills (shoulders, Start/Select) */
  bool pill;
} TouchShape;

typedef struct TouchFinger {
  SDL_FingerID id;
  bool down;
  int control;          /* kTouch* while down */
  float x, y;           /* window pixels */
} TouchFinger;

static GamepadInfo g_touch_gi;          /* the virtual controller (index 0) */
static TouchFinger g_touch_fingers[10];
static uint32 g_touch_held;             /* kGamepadBtn_* bits sent as held */
static uint32 g_touch_down;             /* ... actually under a finger */
/* Pressed since the last frame: kept held for at least one frame, so a
 * quick tap (down and up in one event poll) still reaches the game. */
static uint32 g_touch_latch;
static float g_touch_stick[2][2];       /* knob offset per stick, -1..1 */
static TouchShape g_touch_shape[kTouchControls + 1];
static int g_touch_w, g_touch_h;

static bool TouchPadActive(void) {
  return g_config.player_src[0] == 3;
}
static bool TouchShown(int control) {
  return control == kTouchMenu || (g_config.touch_buttons >> control & 1u);
}

/* Lay the controls out for a w x h output: the D-pad (and left stick) on one
 * side, the face buttons (and right stick) on the other, shoulders above,
 * Start/Select at the bottom centre and the menu button at the top. */
static void TouchLayout(int w, int h) {
  if (w == g_touch_w && h == g_touch_h && g_touch_shape[kTouchA].r > 0) return;
  g_touch_w = w; g_touch_h = h;
  const float u = (float)h * 0.085f * (float)g_config.touch_size / 100.0f;
  const float m = (float)h * 0.04f;
  const bool lh = g_config.touch_left_handed != 0;
  const float left_x = m + 1.7f * u, right_x = (float)w - m - 1.7f * u;
  const float pad_x = lh ? right_x : left_x, face_x = lh ? left_x : right_x;
  const float low_y = (float)h - m - 1.7f * u;
  const float stick_y = low_y - 3.6f * u;
  TouchShape *s = g_touch_shape;
  memset(s, 0, sizeof(g_touch_shape));
  s[kTouchDpad] = (TouchShape){ pad_x, low_y, 1.6f * u };
  s[kTouchLStick] = (TouchShape){ pad_x, stick_y, 1.25f * u };
  s[kTouchRStick] = (TouchShape){ face_x, stick_y, 1.25f * u };
  const float fr = 0.6f * u, fo = 1.05f * u;
  s[kTouchA] = (TouchShape){ face_x + fo, low_y, fr };
  s[kTouchB] = (TouchShape){ face_x, low_y + fo, fr };
  s[kTouchX] = (TouchShape){ face_x, low_y - fo, fr };
  s[kTouchY] = (TouchShape){ face_x - fo, low_y, fr };
  const float sh_y = m + 0.55f * u, sh_w = 0.9f * u, sh_h = 0.45f * u;
  const float lx = lh ? (float)w - m - sh_w : m + sh_w, rx = lh ? m + sh_w : (float)w - m - sh_w;
  const float step = 2.0f * sh_w + 0.3f * u;
  s[kTouchL] = (TouchShape){ lx, sh_y, 0, sh_w, sh_h, true };
  s[kTouchL2] = (TouchShape){ lh ? lx - step : lx + step, sh_y, 0, sh_w, sh_h, true };
  s[kTouchR] = (TouchShape){ rx, sh_y, 0, sh_w, sh_h, true };
  s[kTouchR2] = (TouchShape){ lh ? rx + step : rx - step, sh_y, 0, sh_w, sh_h, true };
  s[kTouchL3] = (TouchShape){ pad_x + (lh ? -2.3f : 2.3f) * u, low_y - 2.0f * u, 0.5f * u };
  s[kTouchR3] = (TouchShape){ face_x + (lh ? 2.3f : -2.3f) * u, low_y - 2.0f * u, 0.5f * u };
  const float bot_y = (float)h - m - 0.35f * u;
  s[kTouchSelect] = (TouchShape){ (float)w * 0.5f - 1.1f * u, bot_y, 0, 0.9f * u, 0.3f * u, true };
  s[kTouchStart] = (TouchShape){ (float)w * 0.5f + 1.1f * u, bot_y, 0, 0.9f * u, 0.3f * u, true };
  s[kTouchMenu] = (TouchShape){ (float)w * 0.5f, m + 0.4f * u, 0, 0.6f * u, 0.32f * u, true };
}

static bool TouchInside(const TouchShape *s, float x, float y, float slack) {
  if (s->pill) return fabsf(x - s->x) <= s->w + slack && fabsf(y - s->y) <= s->h + slack;
  float dx = x - s->x, dy = y - s->y, r = s->r + slack;
  return dx * dx + dy * dy <= r * r;
}

/* The control a touch at (x, y) lands on: the nearest shown one within a
 * little slack, so a thumb need not be exact. */
static int TouchHit(float x, float y) {
  const float slack = (float)g_touch_h * 0.02f;
  for (int c = 0; c <= kTouchMenu; ++c)
    if (TouchShown(c) && TouchInside(&g_touch_shape[c], x, y, c == kTouchDpad ? 0.4f * g_touch_shape[c].r : slack))
      return c;
  return kTouchNone;
}

/* D-pad bits (bit 0 up, 1 down, 2 left, 3 right) for a vector from the
 * D-pad's centre: eight-way, with a small dead centre. */
static unsigned TouchDirections(float dx, float dy, float dead) {
  if (dx * dx + dy * dy < dead * dead) return 0;
  float a = atan2f(-dy, dx) * 4.0f / 3.14159265f;   /* -4..4, 0 = right, 2 = up */
  int seg = (int)floorf(a + 0.5f) & 7;              /* 0 right .. counter-clockwise */
  static const unsigned kDirs[8] = { 8, 8 | 1, 1, 1 | 4, 4, 4 | 2, 2, 2 | 8 };
  return kDirs[seg];
}

static void TouchApply(void) {
  uint32 held = 0;
  g_touch_stick[0][0] = g_touch_stick[0][1] = g_touch_stick[1][0] = g_touch_stick[1][1] = 0;
  for (int i = 0; i < 10; ++i) {
    const TouchFinger *f = &g_touch_fingers[i];
    if (!f->down) continue;
    const TouchShape *s = &g_touch_shape[f->control];
    if (f->control == kTouchDpad || f->control == kTouchLStick || f->control == kTouchRStick) {
      float dx = f->x - s->x, dy = f->y - s->y;
      unsigned dirs = TouchDirections(dx, dy, s->r * 0.25f);
      if (f->control == kTouchRStick) {
        if (dirs & 1) held |= 1u << kGamepadBtn_RsUp;
        if (dirs & 2) held |= 1u << kGamepadBtn_RsDown;
        if (dirs & 4) held |= 1u << kGamepadBtn_RsLeft;
        if (dirs & 8) held |= 1u << kGamepadBtn_RsRight;
      } else {
        if (dirs & 1) held |= 1u << kGamepadBtn_DpadUp;
        if (dirs & 2) held |= 1u << kGamepadBtn_DpadDown;
        if (dirs & 4) held |= 1u << kGamepadBtn_DpadLeft;
        if (dirs & 8) held |= 1u << kGamepadBtn_DpadRight;
      }
      if (f->control != kTouchDpad) {
        float len = sqrtf(dx * dx + dy * dy), k = len > s->r ? s->r / len : 1.0f;
        int st = f->control == kTouchRStick;
        g_touch_stick[st][0] = dx * k / s->r;
        g_touch_stick[st][1] = dy * k / s->r;
      }
    } else if (f->control < kTouchControls && kTouchPadButton[f->control] >= 0) {
      held |= 1u << kTouchPadButton[f->control];
    }
  }
  g_touch_latch |= held & ~g_touch_down;
  g_touch_down = held;
  held |= g_touch_latch;
  uint32 changed = held ^ g_touch_held;
  g_touch_held = held;
  for (int b = 0; b < kGamepadBtn_Count; ++b)
    if (changed >> b & 1u) HandleGamepadInput(&g_touch_gi, b, (held >> b & 1u) != 0);
}

/* The on-screen pad's held buttons (kGamepadBtn_* bits). */
static uint32 TouchPadButtons(void) { return g_touch_gi.modifiers; }

/* Once per frame, before the events: let go of taps that already ended. */
static void TouchPadFrame(void) {
  if (!g_touch_latch) return;
  g_touch_latch = 0;
  TouchApply();
}

/* Release everything (the source changed, or the launcher took the screen). */
static void TouchReset(void) {
  for (int i = 0; i < 10; ++i) g_touch_fingers[i].down = false;
  g_touch_latch = 0;
  TouchApply();
  g_touch_w = g_touch_h = 0;   /* re-lay out: size or hand may have changed */
}

/* A finger event. Returns true when the on-screen gamepad took it. */
static bool TouchPadEvent(const SDL_Event *e) {
  if (!TouchPadActive()) return false;
  const Uint32 type = e->type;
  if (type != TOUCH_FINGER_DOWN && type != TOUCH_FINGER_UP &&
      type != TOUCH_FINGER_MOTION && type != TOUCH_FINGER_CANCELED) return false;
  int w = 0, h = 0;
  SdlRenderer_GetOutputSize(&w, &h);
  if (w <= 0 || h <= 0) return true;
  TouchLayout(w, h);
  const float x = e->tfinger.x * (float)w, y = e->tfinger.y * (float)h;
  const SDL_FingerID id = TOUCH_FINGER_ID(e);
  int slot = -1, free_slot = -1;
  for (int i = 0; i < 10; ++i) {
    if (g_touch_fingers[i].down && g_touch_fingers[i].id == id) slot = i;
    else if (!g_touch_fingers[i].down && free_slot < 0) free_slot = i;
  }
  if (type == TOUCH_FINGER_DOWN) {
    int c = TouchHit(x, y);
    if (c == kTouchMenu) { g_open_launcher_hotkey = 1; return true; }
    if (c == kTouchNone || free_slot < 0) return true;
    g_touch_fingers[free_slot] = (TouchFinger){ id, true, c, x, y };
  } else if (slot >= 0) {
    TouchFinger *f = &g_touch_fingers[slot];
    if (type == TOUCH_FINGER_UP || type == TOUCH_FINGER_CANCELED) {
      f->down = false;
    } else {
      f->x = x; f->y = y;
      /* A thumb may roll from one face button to the next; the D-pad and
       * sticks keep their finger wherever it moves. */
      if (f->control != kTouchDpad && f->control != kTouchLStick && f->control != kTouchRStick) {
        int c = TouchHit(x, y);
        if (c != kTouchNone && c != kTouchMenu && c != kTouchDpad &&
            c != kTouchLStick && c != kTouchRStick)
          f->control = c;
      }
    }
  }
  TouchApply();
  return true;
}

/* ---- drawing -------------------------------------------------------------- */
static void TouchFillCircle(SDL_Renderer *r, float cx, float cy, float rad, TouchColor c) {
  enum { kSeg = 40 };
  SDL_Vertex v[kSeg + 2];
  int idx[kSeg * 3];
  v[0] = (SDL_Vertex){ { cx, cy }, TouchVertexColor(c), { 0, 0 } };
  for (int i = 0; i <= kSeg; ++i) {
    float a = (float)i * 6.2831853f / kSeg;
    v[i + 1] = (SDL_Vertex){ { cx + cosf(a) * rad, cy + sinf(a) * rad }, TouchVertexColor(c), { 0, 0 } };
  }
  for (int i = 0; i < kSeg; ++i) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2; }
  SDL_RenderGeometry(r, NULL, v, kSeg + 2, idx, kSeg * 3);
}
/* A convex outline filled as one fan, so translucent parts never overlap. */
static void TouchFillFan(SDL_Renderer *r, float cx, float cy, const SDL_FPoint *pts, int n, TouchColor c) {
  enum { kMax = 48 };
  SDL_Vertex v[kMax + 1];
  int idx[kMax * 3];
  if (n > kMax) n = kMax;
  v[0] = (SDL_Vertex){ { cx, cy }, TouchVertexColor(c), { 0, 0 } };
  for (int i = 0; i < n; ++i) v[i + 1] = (SDL_Vertex){ pts[i], TouchVertexColor(c), { 0, 0 } };
  for (int i = 0; i < n; ++i) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % n + 1; }
  SDL_RenderGeometry(r, NULL, v, n + 1, idx, n * 3);
}
static void TouchFillPill(SDL_Renderer *r, const TouchShape *s, TouchColor c) {
  enum { kCap = 16 };
  SDL_FPoint pts[2 * (kCap + 1)];
  const float rad = s->h, lx = s->x - s->w + rad, rx = s->x + s->w - rad;
  int n = 0;
  for (int i = 0; i <= kCap; ++i) {   /* right cap, top to bottom */
    float a = -1.5707963f + 3.1415927f * (float)i / kCap;
    pts[n++] = (SDL_FPoint){ rx + cosf(a) * rad, s->y + sinf(a) * rad };
  }
  for (int i = 0; i <= kCap; ++i) {   /* left cap, bottom to top */
    float a = 1.5707963f + 3.1415927f * (float)i / kCap;
    pts[n++] = (SDL_FPoint){ lx + cosf(a) * rad, s->y + sinf(a) * rad };
  }
  TouchFillFan(r, s->x, s->y, pts, n, c);
}

#if SNESRECOMP_SDL3
static void TouchLabel(SDL_Renderer *r, const char *text, float cx, float cy, float height, float alpha) {
  float scale = height / 8.0f;
  if (scale < 1.0f) scale = 1.0f;
  float tw = (float)strlen(text) * 8.0f * scale;
  SDL_SetRenderScale(r, scale, scale);
  SDL_SetRenderDrawColorFloat(r, 0.05f, 0.05f, 0.08f, alpha);
  SDL_RenderDebugText(r, (cx - tw * 0.5f) / scale, (cy - height * 0.5f) / scale, text);
  SDL_SetRenderScale(r, 1.0f, 1.0f);
}
#else
/* SDL2 has no debug text: a 5x7 font holding just the label letters. */
static const uint8 *TouchGlyph(char ch) {
  static const struct { char c; uint8 rows[7]; } kGlyphs[] = {
    { 'A', { 14, 17, 17, 31, 17, 17, 17 } }, { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
    { 'C', { 15, 16, 16, 16, 16, 16, 15 } }, { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
    { 'L', { 16, 16, 16, 16, 16, 16, 31 } }, { 'M', { 17, 27, 21, 21, 17, 17, 17 } },
    { 'N', { 17, 25, 21, 19, 17, 17, 17 } }, { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
    { 'S', { 15, 16, 16, 14, 1, 1, 30 } },   { 'T', { 31, 4, 4, 4, 4, 4, 4 } },
    { 'U', { 17, 17, 17, 17, 17, 17, 14 } }, { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
    { 'Y', { 17, 17, 10, 4, 4, 4, 4 } },     { '2', { 14, 17, 1, 2, 4, 8, 31 } },
    { '3', { 30, 1, 1, 14, 1, 1, 30 } },
  };
  for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++i)
    if (kGlyphs[i].c == ch) return kGlyphs[i].rows;
  return NULL;
}
static void TouchLabel(SDL_Renderer *r, const char *text, float cx, float cy, float height, float alpha) {
  const float px = height / 7.0f;
  const size_t n = strlen(text);
  const float tw = (float)n * 6.0f * px - px;
  float x0 = cx - tw * 0.5f, y0 = cy - height * 0.5f;
  SDL_FRect cells[7 * 5 * 8];
  int count = 0;
  for (size_t i = 0; i < n && i < 8; ++i) {
    const uint8 *g = TouchGlyph(text[i]);
    if (!g) continue;
    for (int row = 0; row < 7; ++row)
      for (int col = 0; col < 5; ++col)
        if (g[row] >> (4 - col) & 1)
          cells[count++] = (SDL_FRect){ x0 + ((float)i * 6.0f + (float)col) * px, y0 + (float)row * px, px, px };
  }
  TouchDrawColor(r, (TouchColor){ 0.05f, 0.05f, 0.08f, alpha });
  if (count) TouchFillRects(r, cells, count);
}
#endif

static void TouchPadDraw(void) {
  if (!TouchPadActive() || !g_renderer) return;
  int w = 0, h = 0;
  SdlRenderer_GetOutputSize(&w, &h);
  if (w <= 0 || h <= 0) return;
  TouchLayout(w, h);
  SDL_Renderer *r = g_renderer;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  const float alpha = (float)g_config.touch_opacity / 100.0f;
  const TouchColor base = { 0.92f, 0.92f, 0.96f, 0.55f * alpha };
  const TouchColor held = { 0.55f, 0.75f, 1.00f, 0.85f * alpha };
  const TouchColor dark = { 0.10f, 0.10f, 0.14f, 0.45f * alpha };
  for (int c = 0; c <= kTouchMenu; ++c) {
    if (!TouchShown(c)) continue;
    const TouchShape *s = &g_touch_shape[c];
    if (c == kTouchDpad) {
      /* A cross, each arm lit while its direction is held. */
      float arm = s->r * 0.36f;
      /* Horizontal bar plus the vertical arms above and below it: no overlap. */
      SDL_FRect bars[3] = { { s->x - s->r, s->y - arm, 2 * s->r, 2 * arm },
                            { s->x - arm, s->y - s->r, 2 * arm, s->r - arm },
                            { s->x - arm, s->y + arm, 2 * arm, s->r - arm } };
      TouchDrawColor(r, base);
      TouchFillRects(r, bars, 3);
      static const struct { int btn; float dx, dy; } kArms[4] = {
        { kGamepadBtn_DpadUp, 0, -1 }, { kGamepadBtn_DpadDown, 0, 1 },
        { kGamepadBtn_DpadLeft, -1, 0 }, { kGamepadBtn_DpadRight, 1, 0 } };
      for (int i = 0; i < 4; ++i) {
        float cx = s->x + kArms[i].dx * s->r * 0.62f, cy = s->y + kArms[i].dy * s->r * 0.62f;
        TouchFillCircle(r, cx, cy, arm * 0.75f, (g_touch_held >> kArms[i].btn & 1u) ? held : dark);
      }
    } else if (c == kTouchLStick || c == kTouchRStick) {
      int st = c == kTouchRStick;
      TouchFillCircle(r, s->x, s->y, s->r, dark);
      TouchFillCircle(r, s->x + g_touch_stick[st][0] * s->r * 0.6f,
                      s->y + g_touch_stick[st][1] * s->r * 0.6f, s->r * 0.48f,
                      (g_touch_stick[st][0] != 0 || g_touch_stick[st][1] != 0) ? held : base);
    } else {
      bool on = c < kTouchControls && kTouchPadButton[c] >= 0 && (g_touch_held >> kTouchPadButton[c] & 1u);
      if (s->pill) TouchFillPill(r, s, on ? held : base);
      else TouchFillCircle(r, s->x, s->y, s->r, on ? held : base);
      const char *label = c == kTouchMenu ? "MENU" : kTouchLabel[c];
      float lh = (s->pill ? s->h : s->r) * 0.9f;
      TouchLabel(r, label, s->x, s->y, lh, 0.9f * alpha + 0.1f);
    }
  }
}

#else
static uint32 TouchPadButtons(void) { return 0; }
static void TouchPadFrame(void) {}
static bool TouchPadActive(void) { return false; }
static bool TouchPadEvent(const SDL_Event *e) { (void)e; return false; }
static void TouchPadDraw(void) {}
static void TouchReset(void) {}
#endif
