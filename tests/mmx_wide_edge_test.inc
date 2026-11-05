/* MMX_WIDE_EDGE_TEST: in widescreen the player's screen-edge wall follows
 * the visible picture, not the native 4:3 one.
 * The camera routine ($00:DE40) stops X 8 px inside the native picture once
 * the camera sits at a room limit. The test pins each camera limit at the
 * live camera and walks into it: with 16:9 the player must stop 8 px inside
 * the widened picture (camera + 248 + margin on the right, camera + 8 -
 * margin on the left), and with widescreen off at the native 8 px. */
static unsigned edge_word(unsigned a) { return g_ram[a] | g_ram[a + 1] << 8; }
static void edge_put(unsigned a, unsigned v) { g_ram[a] = (uint8_t)v; g_ram[a + 1] = (uint8_t)(v >> 8); }
static int edge_walk(unsigned limit, unsigned pad, unsigned frames) {
  const unsigned cam = edge_word(0x1e4d);
  edge_put(limit, cam);
  for (unsigned t = 0; t < frames; ++t) {
    int w, h; MmxPrepareFrame(1280, 720, &w, &h);
    edge_put(limit, cam); /* keep the camera pinned for the whole walk */
    frame(pad);
  }
  check(edge_word(0x1e4d) == cam, "the pinned camera does not scroll");
  return (int)edge_word(0x0bad) - (int)cam;
}
static void wide_edge_checks(void) {
  hitbox_boot();
  zero_replay(60);
  uint8_t *start = malloc(1u << 20);
  size_t n = RtlSaveSnapshotToMemory(start, 1u << 20);
  check(n > 0, "edge test checkpoint saves");
  const unsigned right_limit = edge_word(0x1e58), left_limit = edge_word(0x1e56);
  for (unsigned wide = 0; wide < 2; ++wide) {
    g_config.widescreen = wide != 0;
    g_mmx_custom_aspect = MMX_ASPECT_16_9;
    int w, h; MmxPrepareFrame(1280, 720, &w, &h);
    const int m = MmxWsPlayerEdgeMargin();
    fprintf(stderr, "edge: widescreen=%u margin=%d view=%d\n", wide, m, g_mmx_custom_view.width);
    check(wide ? m > 0 : m == 0, "the margin follows the widescreen setting");

    check(RtlLoadSnapshotFromMemory(start, n), "edge checkpoint restores");
    int right = edge_walk(0x1e58, SNES_PAD_RIGHT, 300);
    edge_put(0x1e58, right_limit);
    fprintf(stderr, "edge: right stop x-cam=%d (want %d)\n", right, 248 + m);
    check(right == 248 + m, "the right wall is 8 px inside the visible picture");

    check(RtlLoadSnapshotFromMemory(start, n), "edge checkpoint restores");
    /* Scroll away from the stage start first: at camera 0 there is no
     * world to the left for the margin to show. */
    for (unsigned t = 0; t < 150; ++t) { MmxPrepareFrame(1280, 720, &w, &h); frame(SNES_PAD_RIGHT); }
    zero_replay(20);
    const int cam = (int)edge_word(0x1e4d);
    check(cam > m, "the camera has scrolled past the margin");
    int left = edge_walk(0x1e56, SNES_PAD_LEFT, 300);
    edge_put(0x1e56, left_limit);
    const int ml = m < cam ? m : cam;
    fprintf(stderr, "edge: left stop x-cam=%d (want %d)\n", left, 8 - ml);
    check(left == 8 - ml, "the left wall is 8 px inside the visible picture");
  }
  g_config.widescreen = false;
  free(start);
  puts("MMX WIDE EDGE CHECKS PASSED");
}
