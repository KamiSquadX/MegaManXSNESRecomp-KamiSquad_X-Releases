#include "mmx_x2ext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "framedump.h"
#include "snes/dsp.h"
#include "mods/mmx_mod_constructor.h"
#include "mmx_netplay.h"
#if SNESRECOMP_NET
#include "netplay/snes_netplay.h"
#endif
#include "mmx_source_assets.h"
#include "mod_runtime.h"
#include "program_module.h"
#include "recomp_launcher.h"
#include "sdl_compat.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include "widescreen.h"
#include "x2_runtime.h"
#include "mmx_x2zero.h"
#include "mmx_x2share.h"
#include "mmx_x2pass.h"
#include "mmx_x2pause.h"
#include "mmx_options.h"
#include "mmx_replay.h"

#define MMX_X2EXT_PACKAGE "megaman-x.xtension.x2"
#define MMX_X2EXT_PAD_L (1u << 10)
#define MMX_X2EXT_PAD_R (1u << 11)

extern uint8_t g_ram[0x20000];
/* Developer mod: Tier 2 diagnostics (mods/mmx_diagnostics_plugin.c). */
extern int mmx_tier2_diagnostics_enabled(void);

/* One program's machine and where it was left. */
typedef struct X2ExtProgram {
  Snes *snes;
  const SnesDesktopHostGame *game;
  const RtlGameInfo *info;
  const SnesProgramModule *module; /* NULL: the stock (X1) tables */
  void *state;
  size_t state_size;
  bool rollback_format; /* the state is a rollback snapshot (X2's password page) */
} X2ExtProgram;

/* X1, X2 at its stage select (and in its stages), X2's password page. The
 * last two share X2's machine. */
static X2ExtProgram s_x1, s_x2, s_x2pw;
static bool s_in_x2pw;
static bool s_enabled;           /* the X-tensions feature is on, X2 ROM loaded */
static bool s_in_x2;
static bool s_swapping;          /* inside swap_programs */
static bool s_x2_booting;        /* first visit: X2 is on its way to stage select */
static unsigned char *s_x2_rom;
static size_t s_x2_rom_size;
static uint16_t s_previous_pad;
static int s_debug = -1;
static bool s_x1_ws_active;      /* X1's widescreen state while X2 runs */
static int s_x1_ws_extra;

static bool debug_enabled(void) {
  if (s_debug < 0) {
    const char *e = getenv("MMX_X2EXT_DEBUG");
    s_debug = e && e[0] && e[0] != '0';
  }
  return s_debug != 0;
}

bool MmxX2ExtInX2(void) { return s_in_x2; }

bool MmxX2ExtSwapping(void) { return s_swapping; }

bool MmxX2ExtInMenu(void) {
  return !(g_ram[0xd0] == 2 && g_ram[0xd1] == 4);
}

void MmxX2ExtSetHost(const SnesDesktopHostGame *x1, const SnesDesktopHostGame *x2) {
  s_x1.game = x1;
  s_x1.info = x1 ? x1->game_info : NULL;
  s_x2.game = s_x2pw.game = x2;
  s_x2.info = s_x2pw.info = &kX2ExtGameInfo;
  s_x2pw.rollback_format = true;
}

/* ── Stage select screens ─────────────────────────────────────────────── */

/* X1's stage select: $D1 = 2, $D2 = 0, with $D3 = 1 while it waits for a
 * choice ($D2 = 2 is the chosen stage's intro). */
static bool x1_stage_select(const uint8_t *r) {
  return r[0xd1] == 2 && r[0xd2] == 0 && r[0xd3] == 1;
}

/* X2's stage select: $D0 = 2 (game), $D1 = 0. Its first frame ($00:96BF)
 * starts a new game (intro stage) when $1FD2 is clear and otherwise runs the
 * stage select ($00:BC69) from $D2 = 1 on. */
static bool x2_stage_select(const uint8_t *r) {
  return r[0xd0] == 2 && r[0xd1] == 0 && r[0xd2] != 0 && r[0x1fd2] != 0;
}

/* ── Background preparation ─────────────────────────────────────────── */

/* X2's first visit used to cold-boot X2 inside the page turn: about 840
 * frames of its power-on, copyright, opening and title, seconds of stall.
 * X2 is now walked to its stage select while X1 runs, a few milliseconds
 * after each X1 frame, so the first page turn is as quick as every later one.
 *
 * A slice sets X1 aside and runs X2 on X2's own machine (its own PPU, APU,
 * DMA and cart), so only what the two programs share has to move: WRAM, and
 * the host-side residue a frame reads and writes (CPU state, APU pacing and
 * port schedule, frame counter, refresh carry, interpreter state, MMIO
 * shadows; RtlHostResidueSave). X2's DSP output is muted meanwhile; nothing
 * X1 plays or shows is touched. In netplay the page turn still does this
 * inside the synchronized switch, on every peer alike.
 *
 * The slices work through jobs:
 *   JOB_BOOT      power-on to the stage select (GAME START), kept as the X2
 *                 page; the title the frame before the confirming Start is
 *                 kept too (s_title), the branch point for the password page.
 *   JOB_PASSWORD  X2 Password Saves: from s_title with PASSWORD chosen, to
 *                 X2's password entry screen, kept as the X2 password page.
 *   JOB_CONFIRM   X1's password was accepted and X2's page was visited: Start
 *                 on the X2 password page, to see whether X2 accepts it.
 *   JOB_FINISH    X2 accepted its password: on to X2's stage select, which
 *                 becomes the X2 page (the password's progress applied). */
typedef enum { JOB_BOOT, JOB_PASSWORD, JOB_CONFIRM, JOB_FINISH, JOB_IDLE } X2Job;
static X2Job s_job = JOB_BOOT;
static bool s_job_started;
static bool s_finish_from_page;       /* JOB_FINISH starts from the password page's state */
static unsigned s_job_frames;

static uint8_t s_park_ram[0x20000];   /* X1's WRAM during a slice */
static uint8_t *s_park_residue;
static Snes *s_park_snes;
static const RtlGameInfo *s_park_info;
static bool s_park_ws_active;
static int s_park_ws_extra;
static FrameDumpCallback s_park_dump;
static uint8_t s_bg_ram[0x20000];     /* X2's WRAM between slices */
static uint8_t *s_bg_residue;
static bool s_bg_active;              /* inside a slice: X2 is current */
static bool s_bg_failed;
static bool s_x2_ready;               /* s_x2.state is X2 at its stage select */
static bool s_x2pw_ready;             /* s_x2pw.state is X2's password entry screen */

static void *s_title;                 /* rollback format */
static size_t s_title_size;
static unsigned s_title_frames;

/* Password screens, from interpreter hooks (both run interpreted): */
enum { RESULT_NONE, RESULT_VALID, RESULT_INVALID };
static bool s_x1pw_screen, s_x2pw_screen; /* the entry screen ran this frame */
static uint8_t s_x1pw_result, s_x2pw_result;
static bool s_x2pw_visited;           /* the X2 password page was opened */
static bool s_x1_accepted;            /* X1's password was taken; X1 heads to its stage select */
static bool s_show_x2pw;              /* X2 refused its password: show its page */
static bool s_x1_inject_start;        /* X1's page comes back with a Start press */

static void x2_first_visit_tick(void);
static unsigned s_boot_frames, s_boot_settled;
static bool s_resume_turn;            /* the page turn back into X2 after a restart */
static uint8_t s_sound_mode;          /* X1's SOUND MODE as last left */
static bool s_played_stage;           /* X2 entered a stage since its stage select */
static void resume_apply(void);

static void quiet_adopt(Snes *snes) {
  /* SnesAdopt without its pacing reset (which also stops mod one-shots):
   * the residue loaded next puts the pacing back exactly. */
  g_snes = snes;
  g_snes_cpu = snes->cpu;
  g_dma = snes->dma;
  g_ppu = snes->ppu;
  g_rom = snes->cart->rom;
  g_sram = snes->cart->ram;
  g_sram_size = snes->cart->ramSize;
}

static void bg_restore(void) {
  quiet_adopt(s_park_snes);
  memcpy(g_ram, s_park_ram, sizeof(s_park_ram));
  RtlHostResidueLoad(s_park_residue);
  g_rtl_game_info = s_park_info;
  snes_program_module_select(s_x1.module);
  snes_mod_runtime_suspend_callbacks_c(0);
  interp_bridge_set_active_pre_opcode_hook_owner(0);
  g_ws_active = s_park_ws_active;
  g_ws_extra = s_park_ws_extra;
  g_framedump_callback = s_park_dump;
  s_bg_active = false;
  RtlApuUnlock();
}

static bool bg_enter(void) {
  const size_t residue = RtlHostResidueSize();
  if (!s_park_residue) s_park_residue = malloc(residue);
  if (!s_bg_residue) s_bg_residue = malloc(residue);
  if (!s_park_residue || !s_bg_residue) return false;
  RtlApuLock();
  memcpy(s_park_ram, g_ram, sizeof(s_park_ram));
  RtlHostResidueSave(s_park_residue);
  s_park_snes = g_snes;
  s_park_info = g_rtl_game_info;
  s_park_ws_active = g_ws_active;
  s_park_ws_extra = g_ws_extra;
  s_park_dump = g_framedump_callback;
  interp_bridge_set_active_pre_opcode_hook_owner(1);
  snes_mod_runtime_suspend_callbacks_c(1);
  snes_program_module_select(s_x2.module);
  g_rtl_game_info = s_x2.info;
  g_ws_active = false;
  g_ws_extra = 0;
  g_framedump_callback = NULL;
  s_bg_active = true;
  if (!s_x2.snes) {
    /* Power-on: a cold boot of X2's own ROM. */
    s_x2.snes = s_x2pw.snes = SnesInit(s_x2_rom, (int)s_x2_rom_size);
    if (!s_x2.snes) {
      fprintf(stderr, "[x2ext] X2 failed to start\n");
      s_bg_failed = true;
      bg_restore();
      return false;
    }
    s_x2_booting = true;
    s_boot_frames = s_boot_settled = 0;
    s_job = JOB_BOOT;
    s_job_started = true;
  } else {
    quiet_adopt(s_x2.snes);
    memcpy(g_ram, s_bg_ram, sizeof(s_bg_ram));
    RtlHostResidueLoad(s_bg_residue);
  }
  dsp_set_output_suppressed(g_snes->apu->dsp, true);
  return true;
}

static void bg_leave(void) {
  memcpy(s_bg_ram, g_ram, sizeof(s_bg_ram));
  RtlHostResidueSave(s_bg_residue);
  bg_restore();
}

static double seconds_now(void) {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

/* Rollback-format snapshots: they load without touching the host's audio
 * timeline (which belongs to X1 during a slice). */
static bool save_rollback(void **buffer, size_t *size) {
  const size_t bound = RtlRollbackSnapshotBound();
  void *b = realloc(*buffer, bound);
  if (!b) return false;
  *buffer = b;
  *size = RtlRollbackSaveToMemory(b, bound);
  return *size != 0;
}

static bool load_rollback(const void *buffer, size_t size) {
  s_swapping = true; /* X2's bridge chunk is not the live Zero's */
  const bool ok = buffer && RtlRollbackLoadFromMemory(buffer, size);
  s_swapping = false;
  return ok;
}

static bool save_program(X2ExtProgram *p);

static void job_done(X2Job next) {
  s_job = next;
  s_job_started = false;
}

/* The next job after X2's stage select is ready. */
static X2Job after_stage_select(void) {
  return MmxX2PassEnabled() && s_title && !s_x2pw_ready ? JOB_PASSWORD : JOB_IDLE;
}

static void job_begin(void) {
  s_job_started = true;
  s_job_frames = 0;
  switch (s_job) {
    case JOB_PASSWORD:
      if (!load_rollback(s_title, s_title_size)) { job_done(JOB_IDLE); return; }
      s_boot_frames = s_title_frames;
      g_ram[0x3c] = 1; /* the title's cursor: PASSWORD ($00:91FD) */
      break;
    case JOB_CONFIRM:
      if (!load_rollback(s_x2pw.state, s_x2pw.state_size)) { job_done(JOB_IDLE); return; }
      s_x2pw_result = RESULT_NONE;
      break;
    case JOB_FINISH:
      if (s_finish_from_page && !load_rollback(s_x2pw.state, s_x2pw.state_size)) {
        job_done(JOB_IDLE);
        return;
      }
      s_finish_from_page = false;
      s_boot_settled = 0;
      break;
    default:
      break;
  }
}

/* After each frame of the current job. */
static void job_tick(void) {
  ++s_job_frames;
  switch (s_job) {
    case JOB_BOOT:
      x2_first_visit_tick();
      if (s_boot_frames >= 6000 && s_x2_booting) {
        fprintf(stderr, "[x2ext] X2 did not reach its stage select; using it as it is\n");
        s_x2_booting = false;
      }
      if (!s_x2_booting) {
        if (save_program(&s_x2)) {
          s_x2_ready = true;
          MmxX2ShareRecordWeapons(g_ram, true);
          fprintf(stderr, "[x2ext] X2 ready at its stage select after %u frames\n", s_boot_frames);
        } else {
          fprintf(stderr, "[x2ext] could not keep X2's stage select\n");
          s_bg_failed = true;
        }
        job_done(after_stage_select());
      }
      break;
    case JOB_PASSWORD: {
      /* Ready once the entry screen has taken input for a few frames. */
      static unsigned held;
      held = s_x2pw_screen && g_ram[0x1e59] == 4 ? held + 1 : 0;
      if (held >= 8) {
        held = 0;
        s_x2pw_ready = save_rollback(&s_x2pw.state, &s_x2pw.state_size);
        fprintf(stderr, "[x2ext] X2's password page %s\n", s_x2pw_ready ? "ready" : "could not be kept");
        job_done(JOB_IDLE);
      } else if (s_job_frames > 2400) {
        fprintf(stderr, "[x2ext] X2's password screen did not open\n");
        job_done(JOB_IDLE);
      }
      break;
    }
    case JOB_CONFIRM:
      if (s_x2pw_result == RESULT_VALID) {
        fprintf(stderr, "[x2ext] X2 took its password too\n");
        /* The X2 page waits for the password's stage select. */
        s_x2_ready = s_x2pw_ready = false;
        s_job = JOB_FINISH; /* continues from here */
        s_job_frames = 0;
        s_boot_settled = 0;
      } else if (s_x2pw_result == RESULT_INVALID || s_job_frames > 30) {
        /* Shown as it is: X2's own refusal (its buzz and message). */
        fprintf(stderr, "[x2ext] X2's password was refused; showing its page\n");
        (void)save_rollback(&s_x2pw.state, &s_x2pw.state_size);
        s_show_x2pw = true;
        job_done(JOB_IDLE);
      }
      break;
    case JOB_FINISH: {
      static uint8_t last[3];
      if (x2_stage_select(g_ram) && !memcmp(last, g_ram + 0xd2, 3)) ++s_boot_settled;
      else s_boot_settled = 0;
      memcpy(last, g_ram + 0xd2, 3);
      if (s_boot_settled >= 60 || s_job_frames > 3000) {
        if (save_program(&s_x2)) s_x2_ready = true;
        MmxX2ShareRecordWeapons(g_ram, true);
        fprintf(stderr, "[x2ext] X2's password applied (%s after %u frames)\n",
                s_boot_settled >= 60 ? "stage select" : "as it is", s_job_frames);
        s_x2pw_visited = false;
        /* A fresh password page for the next visit. */
        s_x2pw_ready = false;
        job_done(after_stage_select());
      }
      break;
    }
    default:
      break;
  }
}

/* Run the jobs for up to `budget` seconds; with budget < 0, until `done`
 * says the caller has what it needs (or nothing is left to do). */
static void bg_run(double budget, bool (*done)(void)) {
  if (s_bg_failed || !s_enabled || s_in_x2) return;
  if (s_job == JOB_IDLE || (done && done())) return;
  const double start = seconds_now();
  if (!bg_enter()) return;
  unsigned frames = 0;
  while (s_job != JOB_IDLE && !s_bg_failed) {
    if (!s_job_started) {
      job_begin();
      if (s_job == JOB_IDLE) break;
    }
    /* The title the frame before each Start pulse: the last one kept before
     * GAME START is taken is the branch point for the password page. */
    if (s_job == JOB_BOOT && MmxX2PassEnabled() && g_ram[0xd0] == 0 &&
        (s_boot_frames + 1) % 24 == 0 && save_rollback(&s_title, &s_title_size))
      s_title_frames = s_boot_frames;
    RtlRunFrame(0);
    job_tick();
    ++frames;
#if SNESRECOMP_NET
    /* Netplay holds every peer while this runs: keep the connection
     * answering, or the peers time each other out. */
    if ((frames & 15) == 0 && MmxNetplayActive()) snes_netplay_keepalive();
#endif
    if (budget >= 0 ? seconds_now() - start >= budget : (done && done())) break;
  }
  if (debug_enabled() && budget < 0)
    fprintf(stderr, "[x2ext] X2's preparation finished in the page turn: %u frames, %.0f ms\n",
            frames, (seconds_now() - start) * 1000.0);
  bg_leave();
}

static bool x2_page_ready(void) { return s_x2_ready; }
static bool x2pw_page_ready(void) { return s_x2pw_ready; }
static bool confirm_resolved(void) { return s_job != JOB_CONFIRM; }

/* ── Program swap ─────────────────────────────────────────────────────── */

static bool save_program(X2ExtProgram *p) {
  if (p->rollback_format) return save_rollback(&p->state, &p->state_size);
  const size_t need = RtlSaveSnapshotToMemory(NULL, 0);
  if (!need) return false;
  void *buffer = realloc(p->state, need);
  if (!buffer) return false;
  p->state = buffer;
  p->state_size = RtlSaveSnapshotToMemory(p->state, need);
  return p->state_size != 0;
}

/* Leave `from` and run `to`. Called by the host at a frame boundary. */
static const SnesDesktopHostGame *swap_programs(X2ExtProgram *from, X2ExtProgram *to) {
  const bool entering_x2 = to != &s_x1;
  /* Not ready yet (a page turn before the background got there, or
   * netplay): finish X2's preparation now. */
  if (to == &s_x2) bg_run(-1, x2_page_ready);
  if (to == &s_x2pw) bg_run(-1, x2pw_page_ready);
  if ((to == &s_x2 && !s_x2_ready) || (to == &s_x2pw && !s_x2pw_ready) || (entering_x2 && !s_x2.snes)) {
    fprintf(stderr, "[x2ext] X2 is not available; staying on X1\n");
#if SNESRECOMP_NET
    snes_netplay_program_switch_done();
#endif
    return NULL;
  }
  /* Heart Tanks, Sub Tanks and lives as the page being left has them (stage
   * selects only: the password pages are before either game has any). */
  /* OPTION MODE's SOUND MODE ($7E:FFCA, $F7 / $F8 in both games). */
  const uint8_t sound_mode = from == &s_x1 ? g_ram[0xffca] : s_sound_mode;
  if (from == &s_x1) s_sound_mode = sound_mode;
  const bool share = from != &s_x2pw && to != &s_x2pw && !s_resume_turn;
  s_resume_turn = false;
  if (share) MmxX2ShareLeave(g_ram, !entering_x2);
  if (!save_program(from)) {
    fprintf(stderr, "[x2ext] could not save the running program; staying\n");
    return NULL;
  }
  from->snes = g_snes;
  /* X2's machine is about to be used in the foreground: a background job
   * part-way through starts again from its snapshot afterwards. */
  if (entering_x2 && s_job != JOB_BOOT) s_job_started = false;
  if (!entering_x2) MmxX2ZeroLeave();
  /* X1's mods hook X1's code by ROM address and tick every frame; none of
   * that means anything inside X2. */
  interp_bridge_set_active_pre_opcode_hook_owner(entering_x2 ? 1 : 0);
  snes_mod_runtime_suspend_callbacks_c(entering_x2);
  snes_program_module_select(to->module);
  /* Tier 2 diagnostics records X2's interpreter coverage too
   * (tier2_x2_*.json), as it does X1's. */
  if (entering_x2) kX2ExtGameInfo.tier2_capture = mmx_tier2_diagnostics_enabled();
  RtlRegisterGame(to->info);
  SnesAdopt(to->snes);
  s_swapping = true;
  const bool loaded = to->rollback_format ? RtlRollbackLoadFromMemory(to->state, to->state_size)
                                          : to->state && RtlLoadSnapshotFromMemory(to->state, to->state_size);
  if (!loaded) fprintf(stderr, "[x2ext] could not restore %s\n", to->game->display_name);
  s_swapping = false;
  if (share) MmxX2ShareEnter(g_ram, entering_x2);
  if (entering_x2) {
    /* Heard again: the background preparation ran it muted. */
    dsp_set_output_suppressed(g_snes->apu->dsp, false);
    /* X1's SOUND MODE, queued as X2's options screen would ($80:8552's
     * ring at $09A2, index $09D3). */
    if ((sound_mode == 0xf7 || sound_mode == 0xf8) && g_ram[0xffca] != sound_mode) {
      g_ram[0xffca] = sound_mode;
      const unsigned i = g_ram[0x9d3] & 0x1e;
      g_ram[0x9a2 + i] = sound_mode;
      g_ram[0x9a3 + i] = 0;
      g_ram[0x9d3] = (uint8_t)((i + 2) & 0x1e);
    }
    /* The session's save file stays X1's: whatever ends the session while X2
     * runs writes X1's cart RAM, never X2's over it. */
    g_sram = s_x1.snes->cart->ram;
    g_sram_size = s_x1.snes->cart->ramSize;
    /* X2 runs at 4:3; X1's widescreen state comes back with X1. */
    s_x1_ws_active = g_ws_active;
    s_x1_ws_extra = g_ws_extra;
    g_ws_active = false;
    g_ws_extra = 0;
  } else {
    g_ws_active = s_x1_ws_active;
    g_ws_extra = s_x1_ws_extra;
  }
  if (entering_x2)
    MmxX2ZeroEnter(s_x1.snes->cart->rom, s_x1.snes->cart->romSize);
  s_in_x2 = entering_x2;
  s_in_x2pw = to == &s_x2pw;
  if (s_in_x2pw) s_x2pw_visited = true;
  s_previous_pad = 0xffff; /* the turning press must not turn back */
  s_x1pw_result = s_x2pw_result = RESULT_NONE;
  fprintf(stderr, "[x2ext] now running %s%s\n", to->game->display_name,
          to == &s_x2pw ? " (password page)" : "");
#if SNESRECOMP_NET
  snes_netplay_program_switch_done();
#endif
  return to->game;
}

static const SnesDesktopHostGame *enter_x2(void) { return swap_programs(&s_x1, &s_x2); }
static const SnesDesktopHostGame *enter_x1(void) { return swap_programs(&s_x2, &s_x1); }
static const SnesDesktopHostGame *enter_x2pw(void) { return swap_programs(&s_x1, &s_x2pw); }
static const SnesDesktopHostGame *leave_x2pw(void) { return swap_programs(&s_x2pw, &s_x1); }

#if SNESRECOMP_NET
/* Netplay: every peer turns the page together, at a tick the host's request
 * holds the match on (snes_netplay_program_switch). arg 1 = X2, 0 = X1. */
static void netplay_switch(int arg) {
  if ((arg == 1) == s_in_x2) {
    /* Already there (a retransmitted request): nothing to turn. */
    snes_netplay_program_switch_done();
    return;
  }
  snesrecomp_desktop_request_program_switch(arg == 1 ? enter_x2 : enter_x1);
}
#endif

/* ── Password screens (X2 Password Saves) ─────────────────────────────── */

/* X1: the entry screen's state dispatch ($00:EE8D, D = $1E48; $1E49 = 4
 * takes input) and Start's check ($00:EF7B, after JSR $F18C: A = 0 when the
 * password is good). X2: the same at $00:EDD3 (D = $1E58, $1E59 = 4) and
 * $00:EEE5 (after JSR $F163: Z when good). Run-ahead's speculative frames
 * are left out. */
static void x1_password_hook(CpuState *cpu, uint32_t pc) {
  if (cpu->D != 0x1e48 || RtlSpeculativeFrame()) return;
  if ((pc & 0x7fffff) == 0x00ee8d) s_x1pw_screen = true;
  else s_x1pw_result = (cpu->A & 0xff) == 0 ? RESULT_VALID : RESULT_INVALID;
}

static void x2_password_hook(CpuState *cpu, uint32_t pc) {
  if (cpu->D != 0x1e58 || RtlSpeculativeFrame()) return;
  if ((pc & 0x7fffff) == 0x00edd3) s_x2pw_screen = true;
  else s_x2pw_result = cpu->_flag_Z ? RESULT_VALID : RESULT_INVALID;
}

static void register_password_hooks(void) {
  static bool done;
  if (done) return;
  done = true;
  (void)interp_bridge_add_pre_opcode_hook(0x00ee8d, x1_password_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x00ef7b, x1_password_hook);
  interp_bridge_set_pre_opcode_hook_owner(1);
  (void)interp_bridge_add_pre_opcode_hook(0x00edd3, x2_password_hook);
  (void)interp_bridge_add_pre_opcode_hook(0x00eee5, x2_password_hook);
  interp_bridge_set_pre_opcode_hook_owner(0);
}

/* X1's frame_begin (mmx_options.c): a Start press handed to X1's password
 * page as it comes back (X2 took its password first). */
void MmxX2ExtX1Inputs(uint32_t *inputs) {
  s_x1pw_screen = false;
  if (!inputs || !s_x1_inject_start || s_in_x2) return;
  s_x1_inject_start = false;
  *inputs |= 1u << 3;
}

/* Per X1 frame on the password page (offline, with X2 Password Saves). */
static void x1_password_page(uint16_t pressed) {
  if (s_x1pw_screen && g_ram[0x1e49] == 4) {
    s_x1_accepted = false;
    if (pressed & MMX_X2EXT_PAD_R) {
      snesrecomp_desktop_request_program_switch(enter_x2pw);
      return;
    }
  }
  const uint8_t result = s_x1pw_result;
  s_x1pw_result = RESULT_NONE;
  if (result != RESULT_VALID) return;
  /* X1 took its password and is on its way to its stage select. */
  s_x1_accepted = true;
  /* Checked when X2's page was filled in, or holds the saved password. */
  if (!s_x2pw_ready || (!s_x2pw_visited && !MmxX2PassHasSaved())) return;
  /* X2's page was filled in: does X2 take its password too? Asked of X2
   * in the background (a few frames); X1 carries on meanwhile. */
  job_done(JOB_CONFIRM);
  bg_run(-1, confirm_resolved);
  if (s_show_x2pw) {
    s_show_x2pw = false;
    snesrecomp_desktop_request_program_switch(enter_x2pw);
  }
}

/* Per X2 frame on X2's password page. */
static void x2_password_page(uint16_t pressed) {
  const uint8_t result = s_x2pw_result;
  s_x2pw_result = RESULT_NONE;
  if (result == RESULT_VALID) {
    /* X2 took its password: back to X1, which takes its own (Start pressed
     * for it) unless it already did; X2 goes on to its stage select in
     * the background. */
    s_finish_from_page = true;
    s_x2_ready = s_x2pw_ready = false;
    job_done(JOB_FINISH);
    s_x1_inject_start = !s_x1_accepted;
    snesrecomp_desktop_request_program_switch(leave_x2pw);
    return;
  }
  if (s_x2pw_screen && g_ram[0x1e59] == 4 && (pressed & MMX_X2EXT_PAD_L))
    snesrecomp_desktop_request_program_switch(leave_x2pw);
}

/* ── Per frame ────────────────────────────────────────────────────────── */

/* Background inputs. JOB_BOOT / JOB_PASSWORD walk the copyright, opening
 * text and title with Start pulses (GAME START is the title's default;
 * JOB_PASSWORD has PASSWORD chosen). GAME START enters $D0 = 2 with
 * $D1 = 0, whose first frame ($00:96BF) opens the stage select instead of
 * the intro stage when $1FD2 is set, so it is set the frame before.
 * JOB_CONFIRM presses Start once on the password page. */
static uint32_t job_inputs(void) {
  ++s_boot_frames;
  switch (s_job) {
    case JOB_BOOT:
      return g_ram[0xd0] != 2 && (s_boot_frames % 24) < 2 ? (1u << 3) : 0;
    case JOB_PASSWORD:
      return !s_x2pw_screen && g_ram[0xd0] != 2 && (s_boot_frames % 24) < 2 ? (1u << 3) : 0;
    case JOB_CONFIRM:
      return s_job_frames < 2 ? (1u << 3) : 0;
    default:
      return 0;
  }
}

/* X2's own soft lock, Flame Stag's stage ($1FAD = 4): the X-Hunter room
 * entered the wrong way, up the ladder into it (around x $0938, y $04AF),
 * starts its boss intro: the player's action becomes $1E ($08:B507, from
 * $0A7D bit 5) and its substate 4 stands until the intro's script lets go,
 * which never comes from that side. There and only there -- $1E straight
 * after the ladder ($26 / $2A), at that spot, a second with nothing letting
 * go -- the player is let go (standing, $00). The front door's real intro is
 * never entered from the ladder, so it is untouched. */
static void flame_stag_ladder_lock(void) {
  static uint8_t last_action;
  static bool from_ladder;
  static unsigned held;
  if (g_ram[0xd0] != 2 || g_ram[0xd1] != 4) { last_action = 0; held = 0; return; }
  const uint8_t action = g_ram[0x09da];
  if (action == 0x1e && last_action != 0x1e) from_ladder = last_action == 0x26 || last_action == 0x2a;
  last_action = action;
  const unsigned x = g_ram[0x09dd] | g_ram[0x09de] << 8, y = g_ram[0x09e0] | g_ram[0x09e1] << 8;
  const bool spot = g_ram[0x1fad] == 4 && x >= 0x0920 && x <= 0x0950 && y >= 0x0490 && y <= 0x04c8;
  if (action != 0x1e || !from_ladder || !spot) { held = 0; return; }
  if (++held < 60) return;
  held = 0;
  g_ram[0x09da] = 0x00;
  g_ram[0x09db] = 0x00;
  last_action = 0;
  fprintf(stderr, "[x2ext] Flame Stag: let go of the X-Hunter room's intro entered up the ladder\n");
}

int MmxX2ExtProgramId(void) {
  if (!s_enabled) return -1;
  return s_in_x2pw ? 2 : s_in_x2 ? 1 : 0;
}

void MmxX2ExtRequestProgram(int program) {
  const int now = MmxX2ExtProgramId();
  if (now < 0 || program == now || program < 0 || program > 2) return;
  if (now == 0) snesrecomp_desktop_request_program_switch(program == 1 ? enter_x2 : enter_x2pw);
  else snesrecomp_desktop_request_program_switch(now == 1 ? enter_x1 : leave_x2pw);
}

void MmxX2ExtFrameEnd(void) {
  if (!s_bg_active) MmxReplayEnd();
}

bool MmxX2ExtFrameBegin(uint32_t *inputs) {
  if (s_bg_active && inputs) {
    *inputs = job_inputs();
    s_x2pw_screen = false;
    return true;
  }
  /* Replays: X2's frames are recorded (and played) like X1's. */
  if (inputs && !MmxReplayBegin(inputs)) return false;
  s_x2pw_screen = false;
  /* Shared items in X2's game modes ($D0 = 2). */
  if (s_enabled && !s_in_x2pw && g_ram[0xd0] == 2) MmxX2ShareFrame(g_ram, true);
  if (!s_in_x2pw) flame_stag_ladder_lock();
  /* OPTION MODE's dash rows, as X1's frames have them. */
  if (!s_in_x2pw) MmxOptionsX2FrameBegin(inputs);
  /* X1 to X2 boss weapons: the pause screen's pages. */
  if (!s_in_x2pw) MmxX2PauseFrameBegin(inputs);
  return inputs ? MmxX2ZeroFrameBegin(*inputs) : true;
}

void MmxX2ExtX1Frame(uint8_t *ram) {
  /* Shared items in X1's game modes ($D1 = 2: stage select and stages). */
  if (s_enabled && !s_in_x2 && !s_bg_active && ram[0xd1] == 2) MmxX2ShareFrame(ram, false);
}

static void x2_first_visit_tick(void) {
  if (debug_enabled()) {
    static uint8_t seen[4] = {0xff, 0xff, 0xff, 0xff};
    if (memcmp(seen, g_ram + 0xd0, 4)) {
      memcpy(seen, g_ram + 0xd0, 4);
      fprintf(stderr, "[x2ext] boot frame %u: D0..D3 = %02X %02X %02X %02X\n", s_boot_frames,
              seen[0], seen[1], seen[2], seen[3]);
    }
  }
  if (g_ram[0xd0] == 2 && g_ram[0xd1] == 0 && g_ram[0xd2] == 0 &&
      g_ram[0x1fd2] == 0)
    g_ram[0x1fd2] = 1;
  /* Ready once its mode bytes have held still for a second. */
  static uint8_t last[3];
  if (x2_stage_select(g_ram) && !memcmp(last, g_ram + 0xd2, 3)) {
    if (++s_boot_settled >= 60) s_x2_booting = false;
  } else {
    s_boot_settled = 0;
  }
  memcpy(last, g_ram + 0xd2, 3);
}

void MmxX2ExtAfterFrame(const SnesDesktopHostFrameStats *stats) {
  (void)stats;
  if (!s_enabled || !g_snes) return;
  resume_apply();
  /* MMX_X2EXT_DEBUG: frames that took long enough to be seen. */
  if (debug_enabled() && stats && stats->guest_seconds > 0.008)
    fprintf(stderr, "[x2ext] slow frame %u in %s: %.1f ms (D0..D3 %02X %02X %02X %02X)\n",
            stats->frame, s_in_x2 ? "X2" : "X1", stats->guest_seconds * 1000.0,
            g_ram[0xd0], g_ram[0xd1], g_ram[0xd2], g_ram[0xd3]);
  /* Offline, X2 gets ready in the background while X1 runs. */
  if (!s_in_x2 && !MmxNetplayActive()) bg_run(0.003, NULL);
  const uint16_t pad = (uint16_t)g_snes->input1_currentState;
  const uint16_t pressed = (uint16_t)(pad & ~s_previous_pad);
  s_previous_pad = pad;
  if (MmxNetplayActive()) {
#if SNESRECOMP_NET
    /* Player 1 (the host's seat) turns the page for everyone; the host's
     * own buttons are never a prediction. */
    if (!snes_netplay_is_host() || snes_netplay_menu_paused()) return;
    if (s_in_x2 ? (pressed & MMX_X2EXT_PAD_L) && x2_stage_select(g_ram)
                : (pressed & MMX_X2EXT_PAD_R) && x1_stage_select(g_ram))
      (void)snes_netplay_program_switch(s_in_x2 ? 0 : 1);
#endif
    return;
  }
  /* X2 Password Saves: R / L between the two password screens. */
  if (s_in_x2pw) {
    x2_password_page(pressed);
    return;
  }
  if (!s_in_x2 && MmxX2PassEnabled()) x1_password_page(pressed);
  if (debug_enabled() && (pressed & (MMX_X2EXT_PAD_L | MMX_X2EXT_PAD_R)))
    fprintf(stderr, "[x2ext] %s: %c pressed, $D1=%02X $D2=%02X $D3=%02X\n",
            s_in_x2 ? "X2" : "X1", (pressed & MMX_X2EXT_PAD_R) ? 'R' : 'L',
            g_ram[0xd1], g_ram[0xd2], g_ram[0xd3]);
  if (s_in_x2) {
    /* X2 Password Saves: X2's progress as a password, half a second after
     * each arrival at its stage select (X2 itself shows one only after a
     * game over), so the next session's password page has it. */
    /* Only on coming back from a stage: a page turn onto an untouched X2
     * never replaces the saved password. */
    static unsigned at_select;
    if (g_ram[0xd0] == 2 && g_ram[0xd1] == 4) s_played_stage = true;
    at_select = x2_stage_select(g_ram) ? at_select + 1 : 0;
    if (at_select == 30 && s_played_stage && MmxX2PassEnabled()) {
      s_played_stage = false;
      MmxX2PassSaveProgress();
    }
    if ((pressed & MMX_X2EXT_PAD_L) && x2_stage_select(g_ram))
      snesrecomp_desktop_request_program_switch(enter_x1);
  } else if ((pressed & MMX_X2EXT_PAD_R) && x1_stage_select(g_ram)) {
    snesrecomp_desktop_request_program_switch(enter_x2);
  }
  /* Testing: MMX_X2EXT_WATCH=<hex addr>,... prints WRAM bytes as they change. */
  static unsigned watch[16], watch_count, watch_ready;
  static uint8_t watch_last[16];
  if (!watch_ready) {
    watch_ready = 1;
    const char *e = getenv("MMX_X2EXT_WATCH");
    while (e && *e && watch_count < 16) {
      char *end;
      unsigned long a = strtoul(e, &end, 16);
      if (end == e) break;
      watch[watch_count] = (unsigned)a & 0x1ffff;
      watch_last[watch_count++] = 0xff;
      e = *end == ',' ? end + 1 : end;
    }
  }
  if (watch_count && stats) {
    char line[256];
    int n = 0;
    for (unsigned i = 0; i < watch_count; ++i)
      if (g_ram[watch[i]] != watch_last[i]) {
        watch_last[i] = g_ram[watch[i]];
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %04X=%02X", watch[i], watch_last[i]);
        if (n > (int)sizeof(line) - 16) break;
      }
    if (n) fprintf(stderr, "[x2ext-watch] %u %s%s\n", stats->frame, s_in_x2 ? "X2" : "X1", line);
  }
  /* Testing: MMX_X2EXT_SWAP_AT=<frame> turns the page once at that frame. */
  static long swap_at = -2;
  if (swap_at == -2) {
    const char *e = getenv("MMX_X2EXT_SWAP_AT");
    swap_at = e ? atol(e) : -1;
  }
  if (swap_at > 0 && stats && stats->frame == (unsigned)swap_at) {
    snesrecomp_desktop_request_program_switch(s_in_x2 ? enter_x1 : enter_x2);
  }
}

/* ── In-game launcher restart ──────────────────────────────────────────
 * A launcher change a running game cannot take (mods, renderer, audio rate)
 * restarts the program from a saved moment. That file is X1's machine (the
 * session starts in X1); X2's page goes beside it in <path>.x2, with the
 * shared progress, and X2 is turned back to if it was running. */

#define RESUME_MAGIC "MMX2-RESUME 1\n"
enum { RESUME_BLOBS = 4 };

static void *s_resume_blob[RESUME_BLOBS];
static size_t s_resume_size[RESUME_BLOBS];
static bool s_resume_pending, s_resume_in_x2;

static bool write_blob(FILE *f, const void *data, size_t size) {
  const uint32_t n = data ? (uint32_t)size : 0;
  return fwrite(&n, sizeof(n), 1, f) == 1 && (!n || fwrite(data, 1, n, f) == n);
}

int MmxX2ExtSaveResume(const char *path) {
  char side[1100];
  snprintf(side, sizeof(side), "%s.x2", path);
  remove(side);
  if (!s_enabled || !s_x1.game) return RtlSaveSnapshot(path);
  const bool in_x2 = s_in_x2 && !s_in_x2pw;
  /* The restart resumes on X1 with X2 just left; the live session (if the
   * restart does not happen) keeps its own record. */
  uint8_t live_share[512];
  const size_t live_size = MmxX2ShareBlob(live_share, sizeof(live_share));
  if (in_x2) MmxX2ShareLeave(g_ram, true);
  /* X1's moment: the running machine, or X1 as it was put aside. */
  bool ok;
  if (s_in_x2) {
    if (in_x2 && save_program(&s_x2)) s_x2_ready = true;
    FILE *f = s_x1.state ? fopen(path, "wb") : NULL;
    ok = f && fwrite(s_x1.state, 1, s_x1.state_size, f) == s_x1.state_size;
    if (f && fclose(f)) ok = false;
  } else {
    ok = RtlSaveSnapshot(path);
  }
  uint8_t share[512];
  const size_t share_size = MmxX2ShareBlob(share, sizeof(share));
  if (live_size <= sizeof(live_share)) MmxX2ShareSetBlob(live_share, live_size);
  if (!ok || !s_x2_ready || !s_x2.state) return ok;
  FILE *f = fopen(side, "wb");
  if (!f) return ok;
  const uint32_t flags = in_x2 ? 1 : 0;
  bool good = fwrite(RESUME_MAGIC, 1, sizeof(RESUME_MAGIC) - 1, f) == sizeof(RESUME_MAGIC) - 1 &&
              fwrite(&flags, sizeof(flags), 1, f) == 1 &&
              write_blob(f, s_x2.state, s_x2.state_size) &&
              write_blob(f, s_title, s_title_size) &&
              write_blob(f, s_x2pw_ready ? s_x2pw.state : NULL, s_x2pw.state_size) &&
              write_blob(f, share_size <= sizeof(share) ? share : NULL, share_size);
  if (fclose(f)) good = false;
  if (!good) remove(side);
  return ok;
}

void MmxX2ExtResumeLoaded(const char *path, int loaded) {
  char side[1100];
  snprintf(side, sizeof(side), "%s.x2", path);
  FILE *f = fopen(side, "rb");
  if (!f) return;
  char magic[sizeof(RESUME_MAGIC) - 1];
  uint32_t flags = 0;
  bool good = fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
              !memcmp(magic, RESUME_MAGIC, sizeof(magic)) && fread(&flags, sizeof(flags), 1, f) == 1;
  for (unsigned i = 0; good && i < RESUME_BLOBS; ++i) {
    uint32_t n = 0;
    good = fread(&n, sizeof(n), 1, f) == 1 && n <= 64u * 1024 * 1024;
    if (!good || !n) continue;
    s_resume_blob[i] = malloc(n);
    good = s_resume_blob[i] && fread(s_resume_blob[i], 1, n, f) == n;
    s_resume_size[i] = n;
  }
  fclose(f);
  remove(side);
  if (!good || !loaded || !s_resume_blob[0]) {
    for (unsigned i = 0; i < RESUME_BLOBS; ++i) { free(s_resume_blob[i]); s_resume_blob[i] = NULL; }
    return;
  }
  s_resume_pending = true;
  s_resume_in_x2 = flags & 1;
}

/* After the first frame with the extension active: X2's page as it was. */
static void resume_apply(void) {
  if (!s_resume_pending || s_bg_active) return;
  s_resume_pending = false;
  /* X2's machine (power-on only; the page's state replaces it). */
  if (!s_x2.snes) {
    if (!bg_enter()) return;
    s_x2_booting = false;
    bg_leave();
  }
  free(s_x2.state);
  s_x2.state = s_resume_blob[0];
  s_x2.state_size = s_resume_size[0];
  s_x2_ready = true;
  if (s_resume_blob[1]) {
    free(s_title);
    s_title = s_resume_blob[1];
    s_title_size = s_resume_size[1];
  }
  if (s_resume_blob[2]) {
    free(s_x2pw.state);
    s_x2pw.state = s_resume_blob[2];
    s_x2pw.state_size = s_resume_size[2];
    s_x2pw_ready = true;
  }
  if (s_resume_blob[3]) MmxX2ShareSetBlob(s_resume_blob[3], s_resume_size[3]);
  free(s_resume_blob[3]);
  for (unsigned i = 0; i < RESUME_BLOBS; ++i) s_resume_blob[i] = NULL;
  job_done(after_stage_select());
  fprintf(stderr, "[x2ext] X2's page kept through the restart%s\n",
          s_resume_in_x2 ? "; back to X2" : "");
  if (s_resume_in_x2) {
    s_resume_turn = true;
    snesrecomp_desktop_request_program_switch(enter_x2);
  }
}

/* ── Mod plugin ───────────────────────────────────────────────────────── */

static void activate(void) {
  const RecompLauncherCModProvider *provider = snes_mod_runtime_launcher_provider_c();
  RecompLauncherCModResource resource = {0};
  char error[512];
  if (!provider || !provider->feature_resource_get ||
      !provider->feature_resource_get(provider->ctx, MMX_X2EXT_PACKAGE, "game", 0,
                                      &resource) || !resource.path[0]) {
    fprintf(stderr, "[x2ext] no X2 ROM selected\n");
    return;
  }
  free(s_x2_rom);
  s_x2_rom = NULL;
  s_x2_rom_size = 0;
  if (!MmxSourceRomLoad(resource.path, 2, &s_x2_rom, &s_x2_rom_size, error,
                        sizeof(error))) {
    fprintf(stderr, "[x2ext] %s\n", error);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot prepare the X2 game extension",
                             error, NULL);
    return;
  }
  s_x2.module = s_x2pw.module = MmxX2ExtModule();
  if (!s_x2.module) {
    fprintf(stderr, "[x2ext] this build has no X2 program linked (%u module(s))\n",
            snes_program_module_count());
    return;
  }
  if (!snes_program_module_rom_matches(s_x2.module, s_x2_rom, s_x2_rom_size)) {
    fprintf(stderr, "[x2ext] this build's X2 program (%u bytes) does not match the "
            "selected ROM (%u bytes)\n", (unsigned)s_x2.module->rom_size,
            (unsigned)s_x2_rom_size);
    return;
  }
  s_enabled = true;
  register_password_hooks();
  MmxX2ShareRegisterHooks();
  MmxOptionsRegisterX2Hooks();
#if SNESRECOMP_NET
  snes_netplay_set_program_switch_handler(netplay_switch);
#endif
  fprintf(stderr, "[x2ext] X2 game extension ready (R on the stage select)\n");
}

SNES_MOD_CONSTRUCTOR(mmx_register_x2ext_plugin) {
  (void)snes_mod_register_activation_plugin(MMX_X2EXT_PACKAGE, activate);
}
