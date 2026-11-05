/* MMX_NETPLAY_COST_TEST: what one netplay frame costs, part by part.
 * A rollback peer runs, per frame: the guest frame, one rollback snapshot,
 * the state digest, and the draw; a misprediction also loads a snapshot and
 * re-simulates the frames since. This times each of those on Highway with the
 * same input script, solo and co-op, with the developer diagnostics off/on.
 * Output: netplay-cost.csv and one stderr line per configuration. */
#include "snes_state_digest.h"
#if defined(_WIN32)
/* MMX_COST_PROFILE=1: sample the test thread every millisecond and print the
 * functions it was in (PDB symbols via DbgHelp). */
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
enum { NETCOST_MAX_SAMPLES = 1 << 20 };
static DWORD64 *netcost_samples;
static volatile LONG netcost_sample_count, netcost_sampling;
static HANDLE netcost_target;
static DWORD WINAPI netcost_sampler(LPVOID unused) {
  (void)unused;
  while (InterlockedCompareExchange(&netcost_sampling, 1, 1)) {
    if (SuspendThread(netcost_target) != (DWORD)-1) {
      CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(netcost_target, &c) && netcost_sample_count < NETCOST_MAX_SAMPLES)
        netcost_samples[netcost_sample_count++] = c.Rip;
      ResumeThread(netcost_target);
    }
    Sleep(1);
  }
  return 0;
}
static HANDLE netcost_profile_begin(void) {
  if (!getenv("MMX_COST_PROFILE")) return NULL;
  if (!netcost_samples) netcost_samples = (DWORD64 *)malloc(sizeof(DWORD64) * NETCOST_MAX_SAMPLES);
  netcost_sample_count = 0;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &netcost_target,
                  0, FALSE, DUPLICATE_SAME_ACCESS);
  InterlockedExchange(&netcost_sampling, 1);
  return CreateThread(NULL, 0, netcost_sampler, NULL, 0, NULL);
}
typedef struct { char name[96]; unsigned count; } NetcostFn;
static int netcost_fn_compare(const void *a, const void *b) {
  return (int)((const NetcostFn *)b)->count - (int)((const NetcostFn *)a)->count;
}
static void netcost_profile_end(HANDLE thread, const char *label) {
  if (!thread) return;
  InterlockedExchange(&netcost_sampling, 0);
  WaitForSingleObject(thread, INFINITE); CloseHandle(thread); CloseHandle(netcost_target);
  static int sym_ready;
  if (!sym_ready) { SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS); SymInitialize(GetCurrentProcess(), NULL, TRUE); sym_ready = 1; }
  static NetcostFn fns[4096]; unsigned nf = 0;
  for (LONG i = 0; i < netcost_sample_count; ++i) {
    char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    memset(buf, 0, sizeof(buf)); si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
    DWORD64 disp = 0; const char *name = SymFromAddr(GetCurrentProcess(), netcost_samples[i], &disp, si) ? si->Name : "?";
    unsigned k; for (k = 0; k < nf && strcmp(fns[k].name, name); ++k) {}
    if (k == nf && nf < 4096) { snprintf(fns[nf].name, sizeof(fns[nf].name), "%s", name); fns[nf++].count = 0; }
    if (k < nf) fns[k].count++;
  }
  qsort(fns, nf, sizeof(fns[0]), netcost_fn_compare);
  fprintf(stderr, "profile %s: %ld samples\n", label, (long)netcost_sample_count);
  for (unsigned k = 0; k < nf && k < 40; ++k)
    fprintf(stderr, "  %5.1f%%  %s\n", 100.0 * fns[k].count / (netcost_sample_count ? netcost_sample_count : 1), fns[k].name);
  /* MMX_COST_LINES=<function>: the hottest source lines inside it. */
  const char *focus = getenv("MMX_COST_LINES");
  if (focus) {
    static struct { unsigned line, count; } lines[8192]; unsigned nl = 0;
    for (LONG i = 0; i < netcost_sample_count; ++i) {
      char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
      memset(buf, 0, sizeof(buf)); si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
      DWORD64 disp = 0;
      if (!SymFromAddr(GetCurrentProcess(), netcost_samples[i], &disp, si) || strcmp(si->Name, focus)) continue;
      IMAGEHLP_LINE64 li; memset(&li, 0, sizeof(li)); li.SizeOfStruct = sizeof(li); DWORD d2 = 0;
      if (!SymGetLineFromAddr64(GetCurrentProcess(), netcost_samples[i], &d2, &li)) continue;
      unsigned k; for (k = 0; k < nl && lines[k].line != li.LineNumber; ++k) {}
      if (k == nl && nl < 8192) { lines[nl].line = li.LineNumber; lines[nl++].count = 0; }
      if (k < nl) lines[k].count++;
    }
    for (unsigned a = 0; a < nl; ++a) for (unsigned b = a + 1; b < nl; ++b)
      if (lines[b].count > lines[a].count) { unsigned t = lines[a].line, c = lines[a].count;
        lines[a] = lines[b]; lines[b].line = t; lines[b].count = c; }
    for (unsigned a = 0; a < nl && a < 30; ++a)
      fprintf(stderr, "    line %u: %u\n", lines[a].line, lines[a].count);
  }
}
#else
static void *netcost_profile_begin(void) { return NULL; }
static void netcost_profile_end(void *t, const char *l) { (void)t; (void)l; }
#endif
static int netcost_compare(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
static unsigned netcost_input(unsigned i) {
  /* Run right, jump and shoot -- busy enough to keep enemies and shots live. */
  unsigned keys = (i / 240) % 4 == 3 ? SNES_PAD_LEFT : SNES_PAD_RIGHT;
  if (i % 40 < 14) keys |= SNES_PAD_B;
  if (i % 6 < 3) keys |= SNES_PAD_Y;
  return keys;
}
static void netcost_run(const char *label, uint8_t *start, size_t n, uint8_t *snap, size_t cap,
                        FILE *csv) {
  enum { N = 600, RESIM = 6 };
  static double total[N];
  static uint32_t pixels[MMX_RENDER_MAX_WIDTH * 224];
  static uint8_t ring[RESIM + 1][1u << 20];
  static size_t ring_n[RESIM + 1];
  const double scale = 1000.0 / (double)SDL_GetPerformanceFrequency();
  double sim = 0, save = 0, digest = 0, draw = 0, load = 0, resim = 0;
  unsigned resims = 0;
  uint64_t image_hash = UINT64_C(14695981039346656037);
  check(RtlLoadSnapshotFromMemory(start, n), "cost scene restores");
  void *profiler = (void *)netcost_profile_begin();
  for (unsigned i = 0; i < N; ++i) {
    uint64_t t0 = SDL_GetPerformanceCounter();
    frame(netcost_input(i));
    uint64_t t1 = SDL_GetPerformanceCounter();
    size_t sn = RtlRollbackSaveToMemory(snap, cap);
    check(sn != 0 && sn <= sizeof(ring[0]), "rollback snapshot saves");
    memcpy(ring[i % (RESIM + 1)], snap, sn); ring_n[i % (RESIM + 1)] = sn;
    uint64_t t2 = SDL_GetPerformanceCounter();
    (void)snes_state_digest(SNES_DIGEST_PART_MASTER);
    uint64_t t3 = SDL_GetPerformanceCounter();
    (void)MmxRendererDraw(pixels, g_mmx_custom_view, true);
    uint64_t t4 = SDL_GetPerformanceCounter();
    { const size_t w = (size_t)(g_mmx_custom_renderer ? g_mmx_custom_view.width : 256);
      for (size_t k = 0; k < w * 224; ++k) { image_hash ^= pixels[k]; image_hash *= UINT64_C(1099511628211); }
      image_hash ^= snes_state_digest(SNES_DIGEST_PART_MASTER); image_hash *= UINT64_C(1099511628211); }
    sim += (t1 - t0) * scale; save += (t2 - t1) * scale; digest += (t3 - t2) * scale;
    draw += (t4 - t3) * scale;
    total[i] = (t4 - t0) * scale;
    /* A late remote input every half second: rewind RESIM frames and replay. */
    if (i >= RESIM && i % 30 == 29) {
      uint64_t r0 = SDL_GetPerformanceCounter();
      unsigned from = i - RESIM;
      check(RtlRollbackLoadFromMemory(ring[from % (RESIM + 1)], ring_n[from % (RESIM + 1)]),
            "rollback snapshot loads");
      uint64_t r1 = SDL_GetPerformanceCounter();
      for (unsigned k = from + 1; k <= i; ++k) {
        frame(netcost_input(k));
        size_t sn2 = RtlRollbackSaveToMemory(snap, cap);
        memcpy(ring[k % (RESIM + 1)], snap, sn2); ring_n[k % (RESIM + 1)] = sn2;
      }
      uint64_t r2 = SDL_GetPerformanceCounter();
      load += (r1 - r0) * scale; resim += (r2 - r1) * scale; ++resims;
    }
  }
  netcost_profile_end(profiler, label);
  qsort(total, N, sizeof(total[0]), netcost_compare);
  double mean = 0; for (unsigned i = 0; i < N; ++i) mean += total[i];
  mean /= N;
  fprintf(csv, "%s,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", label, N,
          mean, total[N / 2], total[N * 95 / 100], total[N - 1], sim / N, save / N, digest / N,
          draw / N, resims ? load / resims : 0, resims ? resim / resims / RESIM : 0);
  fflush(csv);
  fprintf(stderr, "netcost %-26s frame mean=%.2f p95=%.2f max=%.2f | sim=%.2f save=%.2f digest=%.2f "
          "draw=%.2f | rollback load=%.2f resim/frame=%.2f ms | hash=%016llx\n", label, mean,
          total[N * 95 / 100], total[N - 1], sim / N, save / N, digest / N, draw / N,
          resims ? load / resims : 0, resims ? resim / resims / RESIM : 0, (unsigned long long)image_hash);
}
static void netplay_cost_checks(uint8_t *start, uint8_t *snap, size_t cap) {
  check(getenv("MMX_REPLAY_RECORD") && !strcmp(getenv("MMX_REPLAY_RECORD"), "0"),
        "replay recording is off for the baseline");
  RtlEnableExtendedFrameTiming();
  const bool coop = getenv("MMX_PERF_COOP") != NULL;
  hitbox_boot(); zero_replay(60);
  size_t n = RtlSaveSnapshotToMemory(start, cap);
  check(n != 0, "cost scene saves");
  FILE *csv = fopen("netplay-cost.csv", "a");
  check(csv != NULL, "cost results open");
  for (unsigned wide = 0; wide < 4; ++wide) {
    static const MmxRenderAspect aspects[] = {MMX_ASPECT_16_9, MMX_ASPECT_16_9, MMX_ASPECT_21_9, MMX_ASPECT_32_9};
    g_config.widescreen = wide != 0; g_mmx_custom_aspect = aspects[wide];
    int w, h; MmxPrepareFrame(1280, 720, &w, &h);
    char label[64];
    for (unsigned diag = 0; diag < 4; ++diag) {
      /* 0 none, 1 co-op physics trace, 2 tier-2 coverage, 3 replay recorder */
      MmxCoopSetDiagnosticsEnabled(diag == 1);
      tier2_capture_configure(1, 0, diag == 2);
      MmxReplayEnable(diag == 3);
      static const char *const names[] = {"none", "coop-physics", "tier2", "replay"};
      snprintf(label, sizeof(label), "%s-%s-%s", coop ? "coop" : "solo", ((const char *const[]){"4x3", "16x9", "21x9", "32x9"})[wide],
               names[diag]);
      if (getenv("MMX_COST_ONLY") && !strstr(label, getenv("MMX_COST_ONLY"))) continue;
      netcost_run(label, start, n, snap, cap, csv);
    }
    MmxCoopSetDiagnosticsEnabled(false); tier2_capture_configure(1, 0, 0); MmxReplayEnable(false);
  }
  fclose(csv);
  puts("MMX NETPLAY COST MEASURED");
}
