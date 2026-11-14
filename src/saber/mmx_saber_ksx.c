#include "mmx_saber_ksx.h"

#include <string.h>

#include "../mmx_wide_policy.h"
#include "mmx_saber_attack.h"
#include "mmx_saber_priority.h"
#include "mmx_saber_tuning.h"
#include "mmx_saber_wave_runtime.h"

enum {
  KSX_ENEMY_FIRST = 0x0e68,
  KSX_ENEMY_SLOTS = 15,
  /* SNES joypad word bits. */
  KSX_PAD_UP = 0x0800, KSX_PAD_DOWN = 0x0400,
  KSX_PAD_LEFT = 0x0200, KSX_PAD_RIGHT = 0x0100, KSX_PAD_X = 0x0040,
  KSX_PAD_Y = 0x4000,
  /* GIGA ATTACK's ext code (mmx_key_config.h MMX_EXT_GIGA). */
  KSX_EXT_GIGA = 0x0c,
  /* A restagger must land within this many frames of the boss's last hit. */
  KSX_STAGGER_WINDOW = 90,
  /* X1's Boomerang Cutter contact class (mmx_weapon_weakness.inc X1_CUTTER). */
  BOOMERANG_CLASS = 0x0d,
  KSX_SHARD_COUNT = 9,
  KSX_SHARD_SPAWN_TICK = 26,
  KSX_SHARD_LIFE = 56,
  /* Raikousen is untouchable from its dash (tick 12) to its end. */
  KSX_RAIKOUSEN_GUARD_TICK = 12,
  /* X1's enemy shot pool. */
  KSX_ENEMY_SHOT_FIRST = 0x1428,
  KSX_ENEMY_SHOT_SLOTS = 8,
  /* Rakuhouha is Chameleon Sting's slot: X1 weapon 2, $0BDB = 4. */
  KSX_GIGA_WEAPON = 2,
  KSX_GIGA_SELECT = KSX_GIGA_WEAPON * 2,
  KSX_GIGA_ENERGY = 0x1f86 + KSX_GIGA_WEAPON * 2,
  /* X1's shot-deflect sound ($84:9F09). */
  KSX_BLOCK_SOUND = 0x10,
  KSX_BLOCK_SOUND_GAP = 8
};

/* cos/sin of 22.5 * n degrees, Q8, times 6.5 px per frame. */
static const int16_t kShardVx[KSX_SHARD_COUNT] = {
  1664, 1537, 1177, 637, 0, -637, -1177, -1537, -1664};
static const int16_t kShardVy[KSX_SHARD_COUNT] = {
  0, 637, 1177, 1537, 1664, 1537, 1177, 637, 0};

static bool ksx_enabled;
static bool (*terrain_query)(const uint8_t *ram, int x, int y);

typedef struct KsxStaggerSlot {
  uint8_t active;
  uint8_t kind;
  uint8_t prev_hp;
  uint8_t noted;          /* frames since a KSX hit was noted (capped) */
  uint16_t since_hit;     /* frames since the boss was last hit */
  uint32_t used;          /* priority classes used in this combo */
  uint8_t last_move, last_swing; /* the latest KSX hit's move and swing */
} KsxStaggerSlot;

/* Rakuhouha's shards (Mega Man X Engine obj_player_zero_messenko): nine, one
 * every 22.5 degrees from forward to backward, 6.5 px per frame. Each has
 * its own hit area (MmxSaberKsxGigaTargets). */
typedef struct KsxShard {
  uint8_t active, age;
  int32_t x, y;   /* Q8 world position */
} KsxShard;

typedef struct KsxState {
  bool prev_special, prev_giga, special_held;
  uint8_t learned;          /* skill bits, refreshed each pre-player */
  int32_t sub_x;            /* Q8 horizontal carry for move motion */
  uint16_t speed;           /* Q8 px/frame for damped lunges */
  uint16_t hold_y;
  bool hold_y_valid;
  bool lifted;
  KsxShard shards[KSX_SHARD_COUNT];
  KsxStaggerSlot slots[KSX_ENEMY_SLOTS];
  bool pending;
  uint16_t pending_enemy, pending_projectile;
  uint8_t pending_move;
  /* Enemy shots Rekkyoudan turned back, by slot, with the shot's kind. */
  /* The hit admitted as a restagger this frame: its reaction is the
   * ordinary (non-weakness) stagger. */
  bool restagger;
  uint16_t restagger_enemy, restagger_projectile;
  uint8_t reflected;
  uint8_t reflected_kind[KSX_ENEMY_SHOT_SLOTS];
  /* KSX Zero's own weapon choice, kept here like the X2/X3 packs keep
   * theirs: X1's $0BDB stays on the buster. The Giga Attack selects
   * Rakuhouha for its length and restores the previous choice after it. */
  bool prev_r2;
  bool air_ryuenjin;        /* used since Zero last stood or clung after one */
  bool air_raikousen;       /* likewise for Raikousen in the air */
  bool prev_saber;          /* Y (Shot 1) held last frame */
  bool rakuhouha;
  bool giga_active;
  bool giga_prev;
  /* Rakuhouha's guard: objects touching Zero last frame, and the gap
   * before the next block sound. */
  uint32_t block_touch;
  uint8_t block_gap;
  /* guard() held X1's hurt latches this frame or the last. */
  bool guarding;
} KsxState;
static KsxState st;

static const uint8_t kSkillWeapon[MMX_KSX_SKILL_COUNT] = {
  4, 2, 6, 3, 8, 1, 5, 7,
};
static const char *const kSkillNames[MMX_KSX_SKILL_COUNT] = {
  "RYUENJIN", "RAKUHOUHA", "RAIKOUSEN", "KUUENBU",
  "HYOURETSUZAN", "YOUDANTOTSU", "JUUHAZAN", "REKKYOUDAN",
};

/* The pause menu's input command for each skill (one line break at most). */
static const char *const kSkillCommands[MMX_KSX_SKILL_COUNT] = {
  "UP + SHOT 2", "GIGA ATTACK\nBUTTON (GROUND)", "DASH + SHOT 2",
  "SHOT 2 (AIR)", "DOWN + SHOT 2\n(AIR)", "LEFT/RIGHT +\nSHOT 2 (GROUND)",
  "SHOT 2 (GROUND)",
  "PASSIVE",
};

static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void putword(uint8_t *p, unsigned v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

void MmxSaberKsxSetEnabled(bool enabled) {
  ksx_enabled = enabled;
  MmxSaberKsxReset();
}
bool MmxSaberKsxEnabled(void) { return ksx_enabled; }

unsigned MmxSaberKsxSkillWeapon(MmxKsxSkill skill) {
  return skill < MMX_KSX_SKILL_COUNT ? kSkillWeapon[skill] : 0;
}
const char *MmxSaberKsxSkillName(MmxKsxSkill skill) {
  return skill < MMX_KSX_SKILL_COUNT ? kSkillNames[skill] : "";
}
const char *MmxSaberKsxSkillCommand(MmxKsxSkill skill) {
  return skill < MMX_KSX_SKILL_COUNT ? kSkillCommands[skill] : "";
}
MmxKsxSkill MmxSaberKsxSkillForWeapon(unsigned weapon) {
  for (unsigned i = 0; i < MMX_KSX_SKILL_COUNT; ++i)
    if (kSkillWeapon[i] == weapon) return (MmxKsxSkill)i;
  return MMX_KSX_SKILL_COUNT;
}
bool MmxSaberKsxLearned(const uint8_t *ram, MmxKsxSkill skill) {
  unsigned weapon = MmxSaberKsxSkillWeapon(skill);
  return ram && weapon && (ram[0x1f86 + weapon * 2] & 0x40) != 0;
}
MmxKsxSkill MmxSaberKsxSkillForKind(MmxSaberPadKind kind) {
  switch (kind) {
    case SABER_KIND_RYUENJIN: return MMX_KSX_RYUENJIN;
    case SABER_KIND_RAKUHOUHA: return MMX_KSX_RAKUHOUHA;
    case SABER_KIND_RAIKOUSEN: return MMX_KSX_RAIKOUSEN;
    case SABER_KIND_KUUENBU: return MMX_KSX_KUUENBU;
    case SABER_KIND_HYOURETSUZAN: return MMX_KSX_HYOURETSUZAN;
    case SABER_KIND_YOUDANTOTSU: return MMX_KSX_YOUDANTOTSU;
    case SABER_KIND_JUUHAZAN: return MMX_KSX_JUUHAZAN;
    default: return MMX_KSX_SKILL_COUNT;
  }
}

void MmxSaberKsxSetTerrainQuery(bool (*solid)(const uint8_t *ram, int x, int y)) {
  terrain_query = solid;
}

void MmxSaberKsxReset(void) {
  memset(&st, 0, sizeof(st));
}

bool MmxSaberKsxHeld(void) { return st.special_held; }

bool MmxSaberKsxPurpleSaber(void) {
  return ksx_enabled && (st.learned & (1u << MMX_KSX_REKKYOUDAN));
}

static bool learned(MmxKsxSkill skill) {
  return (st.learned & (1u << skill)) != 0;
}

static void begin_motion(uint8_t *ram) {
  st.sub_x = 0;
  st.hold_y_valid = false;
  st.lifted = false;
  st.speed = 0;
  if (ram) st.hold_y = (uint16_t)word(ram + 0x0bb0);
}

void MmxSaberKsxPrePlayer(uint8_t *ram, MmxSaberKsxInput in) {
  bool special, giga, special_edge, giga_edge;
  MmxSaberPadKind kind = SABER_KIND_NONE;
  uint8_t facing = in.facing & 0x40;
  if (!ksx_enabled || !ram) return;
  st.learned = 0;
  for (unsigned i = 0; i < MMX_KSX_SKILL_COUNT; ++i)
    if (MmxSaberKsxLearned(ram, (MmxKsxSkill)i)) st.learned |= (uint8_t)(1u << i);
  special = (in.pad & KSX_PAD_X) != 0;
  const bool saber = (in.pad & KSX_PAD_Y) != 0;
  const bool saber_edge = saber && !st.prev_saber;
  st.prev_saber = saber;
  giga = (in.ext & KSX_EXT_GIGA) == KSX_EXT_GIGA && in.ext != 0xff;
  special_edge = special && !st.prev_special;
  giga_edge = giga && !st.prev_giga;
  {
    /* With Rakuhouha selected, the buster button (R2) is the Giga Attack. */
    const bool r2 = (in.ext & 0x01) != 0 && in.ext != 0xff;
    if (r2 && !st.prev_r2 && st.rakuhouha) giga_edge = true;
    st.prev_r2 = r2;
  }
  st.prev_special = special;
  st.prev_giga = giga;
  st.special_held = special;

  if (giga_edge && in.grounded && learned(MMX_KSX_RAKUHOUHA) &&
      (ram[KSX_GIGA_ENERGY] & 0x3f) >= MmxSaberTuningRakuhouhaEnergy()) {
    kind = SABER_KIND_RAKUHOUHA;
  } else if (special_edge && !in.wall) {
    const unsigned horizontal = in.pad & (KSX_PAD_LEFT | KSX_PAD_RIGHT);
    const bool air_up = !in.grounded && !in.dash && (in.pad & KSX_PAD_UP) &&
        !st.air_ryuenjin && learned(MMX_KSX_RYUENJIN);
    if (in.dash && (in.grounded || !st.air_raikousen)) {
      /* Aimed by the held direction, as Youdantotsu is, also when it
       * cancels another move. */
      kind = SABER_KIND_RAIKOUSEN;
      if (horizontal == KSX_PAD_LEFT || horizontal == KSX_PAD_RIGHT)
        facing = horizontal == KSX_PAD_RIGHT ? 0x40 : 0;
    } else if (in.dash) kind = SABER_KIND_NONE;  /* one Raikousen per jump */
    else if (in.grounded) {
      if (in.pad & KSX_PAD_UP) kind = SABER_KIND_RYUENJIN;
      else if (horizontal == KSX_PAD_LEFT || horizontal == KSX_PAD_RIGHT) {
        kind = SABER_KIND_YOUDANTOTSU;
        facing = horizontal == KSX_PAD_RIGHT ? 0x40 : 0;
      } else kind = SABER_KIND_JUUHAZAN;
    } else if (air_up) {
      /* Once per jump: again only after landing or a wall. */
      kind = SABER_KIND_RYUENJIN;
    } else {
      kind = (in.pad & KSX_PAD_DOWN) ? SABER_KIND_HYOURETSUZAN :
          SABER_KIND_KUUENBU;
    }
    if (kind != SABER_KIND_NONE &&
        !learned(MmxSaberKsxSkillForKind(kind)))
      kind = SABER_KIND_NONE;
  } else if (saber_edge && !in.grounded && !in.wall && (in.pad & KSX_PAD_UP) &&
             learned(MMX_KSX_KUUENBU)) {
    /* Up + Shot 1 in the air is Kuuenbu too (it replaces the air slash
     * Shot 1 has just started). */
    kind = SABER_KIND_KUUENBU;
  }
  {
    /* Standing or clinging gives the jump's one Ryuenjin back, but not
     * while a Ryuenjin is still running (its planted start is grounded). */
    const MmxSaberAttackSnapshot now = MmxSaberAttackSnapshotGet();
    const bool rising = now.kind == SABER_KIND_RYUENJIN &&
        now.phase != SABER_PHASE_IDLE;
    const bool dashing = now.kind == SABER_KIND_RAIKOUSEN &&
        now.phase != SABER_PHASE_IDLE;
    /* Hyouretsuzan's landing is drawn on its ice spike, with Zero's feet
     * above the ground: only standing once it has ended is a landing. */
    const bool stabbing = now.kind == SABER_KIND_HYOURETSUZAN &&
        now.phase != SABER_PHASE_IDLE;
    if ((in.grounded || in.wall) && !rising && !stabbing) st.air_ryuenjin = false;
    if ((in.grounded || in.wall) && !dashing && !stabbing) st.air_raikousen = false;
  }
  if (kind != SABER_KIND_NONE && MmxSaberAttackStartKsx(kind, facing)) {
    begin_motion(ram);
    /* From the ground too: its rise is this jump's one Ryuenjin. */
    if (kind == SABER_KIND_RYUENJIN) st.air_ryuenjin = true;
    if (kind == SABER_KIND_RAIKOUSEN && !in.grounded) st.air_raikousen = true;
    if (kind == SABER_KIND_RAKUHOUHA) {
      /* Select Rakuhouha for the move and spend its energy. */
      const unsigned energy = ram[KSX_GIGA_ENERGY] & 0x3f;
      if (!st.giga_active) st.giga_prev = st.rakuhouha;
      st.giga_active = true;
      st.rakuhouha = true;
      ram[0x1f12] = 0;  /* rebuild X1's energy HUD */
      ram[KSX_GIGA_ENERGY] = (uint8_t)((ram[KSX_GIGA_ENERGY] & 0xc0) |
          (energy - MmxSaberTuningRakuhouhaEnergy()));
      st.block_touch = 0;
      st.block_gap = 0;
    }
    if (kind == SABER_KIND_RAIKOUSEN && !in.grounded)
      MmxSaberAttackSetKsxAnim(0x49);
  }
}

static bool solid_ahead(const uint8_t *ram, int x, int y, int dir) {
  if (!terrain_query) return false;
  const int fx = x + dir * 8;
  return terrain_query(ram, fx, y - 20) || terrain_query(ram, fx, y - 6) ||
      terrain_query(ram, fx, y + 8);
}

/* Move Zero `q8` (Q8 pixels) along his facing, stopping at a wall. */
static void push_x(uint8_t *ram, int q8, int dir) {
  int x = (int)word(ram + 0x0bad), y = (int)word(ram + 0x0bb0);
  st.sub_x += q8;
  int pixels = st.sub_x >> 8;
  st.sub_x &= 255;
  while (pixels-- > 0) {
    if (solid_ahead(ram, x, y, dir)) { st.sub_x = 0; break; }
    x += dir;
  }
  putword(ram + 0x0bad, (unsigned)x & 0xffff);
}

static int raikousen_speed(unsigned tick) {
  if (tick < 12 || tick > 30) return 0;
  if (tick < 16) return 4;
  if (tick < 18) return 7;
  if (tick < 24) return 14;
  if (tick < 26) return 7;
  if (tick < 29) return 3;
  return 1;
}

static bool committed(MmxSaberPadKind kind) {
  return kind == SABER_KIND_RYUENJIN || kind == SABER_KIND_RAKUHOUHA ||
      kind == SABER_KIND_RAIKOUSEN || kind == SABER_KIND_YOUDANTOTSU ||
      kind == SABER_KIND_JUUHAZAN;
}

/* X1's hurt latches, held by its invulnerability countdown (as Axl's
 * dodge roll does in mmx_axl.c). */
static void guard(uint8_t *ram) {
  st.guarding = true;
  if (!ram[0x0bd8]) ram[0x0bd8] = 8;
  if (!ram[0x0c32]) ram[0x0c32] = 8;
  if (ram[0x0c13] < 4) ram[0x0c13] = 4;
}

void MmxSaberKsxMotionPre(uint8_t *ram) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  if (!ksx_enabled || !ram || s.phase == SABER_PHASE_IDLE) return;
  if (committed(s.kind) && !(s.kind == SABER_KIND_RYUENJIN && s.index == 1)) {
    /* The move owns horizontal motion and facing. */
    ram[0x0bc2] = ram[0x0bc3] = 0;
    ram[0x0c11] = s.facing;
    ram[0x0bb9] = (uint8_t)((ram[0x0bb9] & (uint8_t)~0x40) | s.facing);
  }
  switch (s.kind) {
    case SABER_KIND_RYUENJIN:
      /* Mega Man X Engine player_state_saber_up: 4 frames planted, then a
       * full jump speed (5 px/frame) for 8 frames; gravity carries the rest
       * of the rise. Releasing X stops the rise where it is. */
      if (s.index == 1) {
        if (s.tick == 0 && (int16_t)word(ram + 0x0bc4) > 0)
          putword(ram + 0x0bc4, 0);
      } else if (s.tick <= 3) {
        putword(ram + 0x0bc4, 0);
      } else if (s.tick <= 11) {
        /* The lift itself is applied after native movement (MotionPost). */
        putword(ram + 0x0bc4, 0);
      }
      break;
    case SABER_KIND_HYOURETSUZAN:
      if (s.index == 0 && s.tick >= 4) putword(ram + 0x0bc4, 0x10000 - 0x0500);
      break;
    case SABER_KIND_RAKUHOUHA:
      /* Nothing touches Zero for the whole Giga Attack. */
      guard(ram);
      break;
    case SABER_KIND_RAIKOUSEN:
      if (!(ram[0x0bd3] & 4) && !(ram[0x0bd4] & 4)) {
        if (!st.hold_y_valid) { st.hold_y = (uint16_t)word(ram + 0x0bb0); st.hold_y_valid = true; }
        putword(ram + 0x0bc4, 0);
      }
      if (s.tick >= KSX_RAIKOUSEN_GUARD_TICK) guard(ram);
      break;
    default:
      break;
  }
}

static void forget_dead_reflections(const uint8_t *ram);

void MmxSaberKsxShardsStep(const uint8_t *ram) {
  const MmxSaberAttackSnapshot snap = MmxSaberAttackSnapshotGet();
  const MmxSaberAttackSnapshot *s = &snap;
  if (!ksx_enabled || !ram) return;
  forget_dead_reflections(ram);
  for (unsigned i = 0; i < KSX_SHARD_COUNT; ++i) {
    KsxShard *shard = &st.shards[i];
    if (!shard->active) continue;
    shard->x += kShardVx[i];
    shard->y -= kShardVy[i];
    if (++shard->age >= KSX_SHARD_LIFE) shard->active = 0;
  }
  if (s->kind == SABER_KIND_RAKUHOUHA && s->phase != SABER_PHASE_IDLE &&
      s->tick == KSX_SHARD_SPAWN_TICK) {
    /* Spawned 16 px out from Zero's feet line, as the engine does. */
    const int32_t x = (int32_t)word(ram + 0x0bad), y = (int32_t)word(ram + 0x0bb0);
    for (unsigned i = 0; i < KSX_SHARD_COUNT; ++i) {
      KsxShard *shard = &st.shards[i];
      shard->active = 1;
      shard->age = 0;
      shard->x = (x << 8) + kShardVx[i] * 16 * 2 / 13;
      shard->y = ((y + 14) << 8) - kShardVy[i] * 16 * 2 / 13;
    }
  }
}

unsigned MmxSaberKsxShards(MmxSaberKsxShard *out, unsigned max) {
  unsigned count = 0;
  if (!ksx_enabled || !out) return 0;
  for (unsigned i = 0; i < KSX_SHARD_COUNT && count < max; ++i) {
    if (!st.shards[i].active) continue;
    out[count].world_x = (int16_t)(st.shards[i].x >> 8);
    out[count].world_y = (int16_t)(st.shards[i].y >> 8);
    out[count].angle = (uint8_t)i;
    out[count].age = st.shards[i].age;
    ++count;
  }
  return count;
}

void MmxSaberKsxMotionPost(uint8_t *ram) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  const int dir = (s.facing & 0x40) ? 1 : -1;
  if (!ksx_enabled || !ram || s.phase == SABER_PHASE_IDLE) return;
  switch (s.kind) {
    case SABER_KIND_RYUENJIN:
      if (s.index == 0 && s.tick >= 4 && s.tick <= 11) {
        /* 5 px a frame up, stopping under a ceiling. */
        int x = (int)word(ram + 0x0bad), y = (int)word(ram + 0x0bb0);
        int rise = 5;
        while (rise > 0 && !(terrain_query &&
               (terrain_query(ram, x, y - 27) ||
                terrain_query(ram, x - 6, y - 27) ||
                terrain_query(ram, x + 6, y - 27)))) {
          --y; --rise;
        }
        putword(ram + 0x0bb0, (unsigned)y & 0xffff);
        if (!st.lifted) {
          /* Airborne from the first lift: X1's fall action and gravity. */
          ram[0x0baa] = 8;
          ram[0x0bab] = 0;
          ram[0x0bc6] = 0x40;
          /* Leaving a ladder: X1 clears its on-ladder latch ($64,x) on every
           * native exit. Left set, the fall skips the floor probes and Zero
           * drops through every floor below. */
          ram[0x0c0c] = 0;
          st.lifted = true;
        }
        /* After the last lifted frame X1's air physics carries the full
         * jump speed on, slowing to the top under gravity. */
        putword(ram + 0x0bc4, s.tick == 11 && rise == 0 ? 0x0500 : 0);
      }
      /* move_x(dir) on ticks 4-24: one pixel a frame forward. */
      if (s.index == 0 && s.tick >= 4 && s.tick <= 24) push_x(ram, 0x0100, dir);
      break;
    case SABER_KIND_RAIKOUSEN:
      push_x(ram, raikousen_speed(s.tick) << 8, dir);
      if (st.hold_y_valid) putword(ram + 0x0bb0, st.hold_y);
      break;
    case SABER_KIND_YOUDANTOTSU:
      if (s.tick == 13) st.speed = 0x0850;
      if (s.tick >= 13 && s.tick <= 30) {
        push_x(ram, st.speed, dir);
        st.speed = (uint16_t)((st.speed * 230u) >> 8);
      }
      break;
    case SABER_KIND_JUUHAZAN:
      if (s.tick == 10) st.speed = 0x0550;
      if (s.tick >= 10 && s.tick <= 25) {
        push_x(ram, st.speed, dir);
        st.speed = (uint16_t)((st.speed * 230u) >> 8);
      }
      break;
    default:
      break;
  }
}

/* ---- weapons and the Giga Attack's guard ------------------------------- */

static bool giga_running(void) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  return s.kind == SABER_KIND_RAKUHOUHA && s.phase != SABER_PHASE_IDLE;
}

/* After a guarded move, end X1's latches and its blink at once (as Axl's
 * roll does); during one, keep Zero drawn through the blink. */
bool MmxSaberKsxRakuhouhaSelected(void) {
  return ksx_enabled && st.rakuhouha;
}

bool MmxSaberKsxBusterBlocked(void) {
  /* The player's own choice: a Giga Attack started from the buster leaves
   * R2 charging through it and after it. */
  return ksx_enabled && (st.giga_active ? st.giga_prev : st.rakuhouha);
}

static void select_rakuhouha(uint8_t *ram, bool on) {
  if (st.rakuhouha == on) return;
  st.rakuhouha = on;
  /* As the packs' switch does: rebuild X1's energy HUD ($1F12). */
  ram[0x1f12] = on ? 0 : 4;
}

void MmxSaberKsxShoulders(uint8_t *ram, unsigned pressed) {
  if (!ksx_enabled || !ram || !pressed || st.giga_active) return;
  /* The conditions X1's own cycle ($81:99D3) checks. */
  if (ram[0x1f23] || ram[0x1f31] || ram[0x0baa] == 0x18 || ram[0x0baa] == 0x42)
    return;
  if (!MmxSaberKsxLearned(ram, MMX_KSX_RAKUHOUHA)) {
    select_rakuhouha(ram, false);
    return;
  }
  select_rakuhouha(ram, !st.rakuhouha);
}

void MmxSaberKsxMenuChoice(uint8_t *ram, unsigned item) {
  if (!ksx_enabled || !ram || st.giga_active) return;
  select_rakuhouha(ram, item == KSX_GIGA_WEAPON &&
                            MmxSaberKsxLearned(ram, MMX_KSX_RAKUHOUHA));
}

static void guard_end(uint8_t *ram) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  const bool guarded = s.phase != SABER_PHASE_IDLE &&
      (s.kind == SABER_KIND_RAKUHOUHA ||
       (s.kind == SABER_KIND_RAIKOUSEN && s.tick >= KSX_RAIKOUSEN_GUARD_TICK));
  if (guarded && st.guarding) {
    ram[0x0bb6] = 1;
    return;
  }
  if (st.guarding) {
    ram[0x0c13] = ram[0x0bd8] = ram[0x0c32] = 0;
    ram[0x0bb6] = 1;
    st.guarding = false;
  }
}

static void ksx_sound(uint8_t *ram, unsigned command) {
  const unsigned i = ram[0x0ba3] & 30;
  ram[0x0b72 + i] = (uint8_t)command;
  ram[0x0b73 + i] = 0;
  ram[0x0ba3] = (uint8_t)((i + 2) & 30);
}

static bool boxes_overlap(const int a[4], const int b[4]) {
  return a[0] <= b[2] && b[0] <= a[2] && a[1] <= b[3] && b[1] <= a[3];
}

void MmxSaberKsxPlayerEnd(uint8_t *ram) {
  int zero[4], other[4];
  uint32_t touch = 0;
  if (!ksx_enabled || !ram) return;
  if (st.giga_active && !giga_running()) {
    st.giga_active = false;
    select_rakuhouha(ram, st.giga_prev &&
                              MmxSaberKsxLearned(ram, MMX_KSX_RAKUHOUHA));
  }
  if (st.rakuhouha && !MmxSaberKsxLearned(ram, MMX_KSX_RAKUHOUHA))
    select_rakuhouha(ram, false);
  guard_end(ram);
  if (st.block_gap) --st.block_gap;
  if (!giga_running() || !MmxSaberAttackObjectBox(ram, 0x0ba8, zero)) {
    st.block_touch = 0;
    return;
  }
  /* Rakuhouha blocks: an enemy or enemy shot reaching Zero plays X1's
   * deflect sound instead of hurting him (guard() holds X1's latches). */
  for (unsigned i = 0; i < KSX_ENEMY_SLOTS + KSX_ENEMY_SHOT_SLOTS; ++i) {
    const unsigned object = i < KSX_ENEMY_SLOTS ? KSX_ENEMY_FIRST + i * 64 :
        KSX_ENEMY_SHOT_FIRST + (i - KSX_ENEMY_SLOTS) * 64;
    if (!ram[object] || !MmxSaberAttackObjectBox(ram, object, other) ||
        !boxes_overlap(zero, other))
      continue;
    touch |= 1u << i;
  }
  if ((touch & ~st.block_touch) && !st.block_gap) {
    ksx_sound(ram, KSX_BLOCK_SOUND);
    st.block_gap = KSX_BLOCK_SOUND_GAP;
  }
  st.block_touch = touch;
}

/* Rakuhouha's hit areas: the blast around Zero, then each live shard. */
unsigned MmxSaberKsxGigaTargets(const uint8_t *ram, unsigned tick,
                                uint8_t facing, MmxSaberKsxGigaTarget *out,
                                unsigned max) {
  unsigned count = 0;
  if (!ksx_enabled || !ram || !out) return 0;
  if (tick >= 23 && tick <= 31 && count < max) {
    /* messenko frames 6-9: x -30..36, y -26..30 around Zero. */
    const int x = (int16_t)word(ram + 0x0bad), y = (int16_t)word(ram + 0x0bb0);
    out[count++] = (MmxSaberKsxGigaTarget){
        (int16_t)(x + ((facing & 0x40) ? 3 : -3)), (int16_t)(y - 5), 34, 29};
  }
  /* Each shard's art (four frames, rotated to its angle) about its
   * position: centre offset and half size, plus two pixels. */
  static const int8_t box[KSX_SHARD_COUNT][4] = {
    {20, 0, 24, 18}, {19, -10, 22, 18}, {15, -16, 20, 20}, {10, -19, 18, 23},
    {0, -21, 18, 24}, {-10, -19, 18, 22}, {-16, -16, 20, 19}, {-19, -11, 23, 18},
    {-21, -1, 24, 18}};
  for (unsigned i = 0; i < KSX_SHARD_COUNT && count < max; ++i) {
    if (!st.shards[i].active) continue;
    out[count++] = (MmxSaberKsxGigaTarget){
        (int16_t)((st.shards[i].x >> 8) + box[i][0]),
        (int16_t)((st.shards[i].y >> 8) + box[i][1]),
        (uint8_t)box[i][2], (uint8_t)box[i][3]};
  }
  return count;
}

/* ---- contact class ------------------------------------------------------ */

/* The Maverick each skill's element is the weakness of (X1 object ids):
 * the weapon its own Maverick gave X, against the boss weak to it. The
 * target's damage row (row_weakness) finds every other target weak to it. */
static const uint8_t kSkillWeakBoss[MMX_KSX_SKILL_COUNT] = {
  0x02,  /* Ryuenjin (Fire Wave)          burns Chill Penguin */
  0x52,  /* Rakuhouha (Chameleon Sting)   Storm Eagle */
  0x14,  /* Raikousen (Electric Spark)    Armored Armadillo */
  0x07,  /* Kuuenbu (Rolling Shield)      Launch Octopus */
  0x31,  /* Hyouretsuzan (Shotgun Ice)    freezes Spark Mandrill */
  0x05,  /* Youdantotsu (Homing Torpedo)  Boomer Kuwanger */
  0x0c,  /* Juuhazan (Storm Tornado)      Flame Mammoth */
  0x00,  /* Rekkyoudan is passive */
};
enum { KSX_STING_CHAMELEON = 0x0a };
/* Each skill's element: its weapon's X1 shot class, which X's own shot of
 * that weapon carries in +$0A. Every hit of the skill uses it, for the
 * damage column of the target's row and for the reaction in $1F1D, so the
 * skill meets everything as that weapon would: Mavericks, mini-bosses,
 * enemies, and objects such as the igloos that only Fire Wave breaks. */
static const uint8_t kSkillClass[MMX_KSX_SKILL_COUNT] = {
  0x0a,  /* Ryuenjin: Fire Wave */
  0x0b,  /* Rakuhouha: Chameleon Sting */
  0x0c,  /* Raikousen: Electric Spark */
  0x08,  /* Kuuenbu: Rolling Shield */
  0x0e,  /* Hyouretsuzan: Shotgun Ice ($88:9DF8 freezes Spark Mandrill on
          * $0E/$17) */
  0x07,  /* Youdantotsu: Homing Torpedo */
  0x09,  /* Juuhazan: Storm Tornado */
  0x0d,  /* Rekkyoudan: Boomerang Cutter */
};
/* X1's eight weapon classes, uncharged ($07..$0E). */
enum { KSX_WEAPON_CLASS_FIRST = 0x07, KSX_WEAPON_CLASS_LAST = 0x0e };

static bool finisher_projectile(const uint8_t *ram, unsigned projectile) {
  return projectile >= 0x1228 && projectile < 0x1428 &&
      (projectile & 63) == 0x28 && ram[projectile] &&
      word(ram + projectile + 0x3e) == 0x5a53;
}

/* The moves Rekkyoudan gives Boomerang Cutter's class: every saber slash,
 * the X3 finisher and wave, Juuhazan, Youdantotsu and Kuuenbu. */
static bool boomerang_move(MmxSaberPadKind kind) {
  switch (kind) {
    case SABER_KIND_GROUND1: case SABER_KIND_GROUND2: case SABER_KIND_GROUND3:
    case SABER_KIND_AIR: case SABER_KIND_WALL: case SABER_KIND_DASH:
    case SABER_KIND_JUUHAZAN: case SABER_KIND_YOUDANTOTSU: case SABER_KIND_KUUENBU:
      return true;
    default:
      return false;
  }
}

/* What class `cls` does to `enemy` by its X1 damage row: -1 when the row
 * makes it immune (bit 7) or cannot be read. */
static int column(const uint8_t *ram, unsigned enemy, unsigned cls) {
  unsigned v;
  if (!MmxSaberAttackNativeTableValue(ram[enemy + 0x28], cls, &v)) return -1;
  return (v & 0x80) ? -1 : (int)v;
}

/* The damage of the one X1 weapon the target's row favors above every
 * other weapon (its weakness), or 0 when no single weapon stands out. */
static int weakness_value(const uint8_t *ram, unsigned enemy) {
  int best = 0;
  bool unique = false;
  for (unsigned c = KSX_WEAPON_CLASS_FIRST; c <= KSX_WEAPON_CLASS_LAST; ++c) {
    const int v = column(ram, enemy, c);
    if (v > best) {
      best = v;
      unique = true;
    } else if (v == best) {
      unique = false;
    }
  }
  return unique ? best : 0;
}

/* The target's row makes weapon class `cls` its weakness: a Maverick's 3
 * against 1s, the igloo's Fire Wave, Storm Tornado against the Sigma
 * stages' bosses, and any mini-boss or enemy built the same way. */
static bool row_weakness(const uint8_t *ram, unsigned enemy, unsigned cls) {
  const int v = column(ram, enemy, cls);
  return v > 0 && v == weakness_value(ram, enemy);
}

typedef struct KsxHit {
  unsigned cls;     /* 0: the hit keeps its own class */
  bool weak;        /* the target is weak to it: at least its damage */
  bool keep_damage; /* immune to the class but not to the saber */
} KsxHit;

/* The class a KSX Zero hit takes against `enemy`. A skill always hits as
 * its weapon. Rekkyoudan's moves hit as Boomerang Cutter where that is the
 * target's weakness, and against bosses that are not weak to the move's
 * own element (Boomerang Cutter cuts Mammoth's trunk and Octopus's
 * tentacles); a plain slash, the finisher and the wave always cut. */
static KsxHit ksx_class(const uint8_t *ram, unsigned enemy, unsigned projectile) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  const bool saber = MmxSaberAttackOwnsProjectile(ram, projectile);
  const bool wave = !saber && MmxSaberWaveRuntimeOwns(ram, projectile);
  const bool finisher = !saber && !wave && finisher_projectile(ram, projectile);
  const uint8_t target = ram[enemy + 0x0a];
  KsxHit hit = {0, false, false};
  unsigned element = 0;
  bool element_weak = false;
  int own;
  if (!saber && !wave && !finisher) return hit;
  own = column(ram, enemy, ram[projectile + 0x0a]);
  if (saber) {
    const MmxKsxSkill skill = MmxSaberKsxSkillForKind(s.kind);
    if (skill < MMX_KSX_REKKYOUDAN) {
      element = kSkillClass[skill];
      element_weak = (kSkillWeakBoss[skill] && kSkillWeakBoss[skill] == target) ||
          row_weakness(ram, enemy, element);
    }
  }
  if (learned(MMX_KSX_REKKYOUDAN) && (wave || finisher || boomerang_move(s.kind))) {
    const bool cut_weak = target == KSX_STING_CHAMELEON ||
        row_weakness(ram, enemy, BOOMERANG_CLASS);
    const int cut = column(ram, enemy, BOOMERANG_CLASS);
    bool cut_wins;
    if (!element) cut_wins = true;
    else if (element_weak) cut_wins = false;
    else if (cut_weak) cut_wins = true;
    else if (column(ram, enemy, element) < 0) cut_wins = cut >= 0;
    else cut_wins = cut >= 0 && MmxWidePolicy_IsBossEncounter(target);
    if (cut_wins) {
      hit.cls = BOOMERANG_CLASS;
      hit.weak = cut_weak;
      hit.keep_damage = cut < 0 && own >= 0;
      return hit;
    }
  }
  if (!element) return hit;
  hit.cls = element;
  hit.weak = element_weak;
  /* Never weaker than the saber: a target immune to the weapon but not to
   * the saber takes the saber's damage (the reaction stays the weapon's). */
  hit.keep_damage = column(ram, enemy, element) < 0 && own >= 0;
  return hit;
}

static bool ksx_target(const uint8_t *ram, unsigned enemy) {
  return ksx_enabled && ram && enemy >= KSX_ENEMY_FIRST && enemy < 0x1228 &&
      (enemy & 63) == 0x28;
}

unsigned MmxSaberKsxContactClass(const uint8_t *ram, unsigned enemy,
                                 unsigned projectile, unsigned original,
                                 bool reaction) {
  KsxHit hit;
  if (!ksx_target(ram, enemy)) return original;
  hit = ksx_class(ram, enemy, projectile);
  if (!hit.cls) return original;
  /* A restagger plays the ordinary stagger, unless the hit is the target's
   * weakness: that always keeps its element and reaction (Hyouretsuzan's
   * Shotgun Ice freezes Spark Mandrill, $87:8C81, mid-combo too). */
  if (!hit.weak && st.restagger && st.restagger_enemy == enemy &&
      st.restagger_projectile == projectile)
    return original;
  if (reaction) return hit.cls;
  return hit.keep_damage ? original : (original & 0xff00) | hit.cls;
}

unsigned MmxSaberKsxWeaknessDamage(const uint8_t *ram, unsigned enemy,
                                   unsigned projectile, unsigned value) {
  KsxHit hit;
  int best;
  if (!ksx_target(ram, enemy) || !value || (value & 0x80)) return 0;
  hit = ksx_class(ram, enemy, projectile);
  if (!hit.cls) return 0;
  if (hit.weak && !hit.keep_damage) return value;
  /* A Boss Rush rule gave a relocated X2/X3 body this hit's weapon: its
   * column is the row's weakness column. */
  best = weakness_value(ram, enemy);
  return best && (int)value == best &&
      (int)value != column(ram, enemy, ram[projectile + 0x0a]) ? value : 0;
}

unsigned MmxSaberKsxShotWeapons(const uint8_t *ram, unsigned projectile,
                                unsigned weapons[2]) {
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  bool saber, wave, finisher;
  unsigned count = 0;
  if (!ksx_enabled || !ram) return 0;
  saber = MmxSaberAttackOwnsProjectile(ram, projectile);
  wave = !saber && MmxSaberWaveRuntimeOwns(ram, projectile);
  finisher = !saber && !wave && finisher_projectile(ram, projectile);
  if (saber) {
    const MmxKsxSkill skill = MmxSaberKsxSkillForKind(s.kind);
    if (skill < MMX_KSX_REKKYOUDAN) weapons[count++] = kSkillWeapon[skill];
  }
  if (learned(MMX_KSX_REKKYOUDAN) && (wave || finisher ||
                                     (saber && boomerang_move(s.kind))))
    weapons[count++] = kSkillWeapon[MMX_KSX_REKKYOUDAN];
  return count;
}

/* ---- Rekkyoudan: enemy shots ------------------------------------------- */

static void forget_dead_reflections(const uint8_t *ram) {
  for (unsigned i = 0; i < KSX_ENEMY_SHOT_SLOTS; ++i) {
    const unsigned shot = KSX_ENEMY_SHOT_FIRST + i * 64;
    if ((st.reflected & (1u << i)) &&
        (!ram[shot] || ram[shot + 0x0a] != st.reflected_kind[i]))
      st.reflected &= (uint8_t)~(1u << i);
  }
}

bool MmxSaberKsxEnemyShotHit(uint8_t *ram, unsigned projectile) {
  int saber[4], shot_box[4];
  if (!ksx_enabled || !ram || !MmxSaberAttackOwnsProjectile(ram, projectile))
    return false;
  /* Without Rekkyoudan the saber neither cuts nor turns back shots. */
  if (!learned(MMX_KSX_REKKYOUDAN)) return true;
  if (!MmxSaberAttackObjectBox(ram, projectile, saber)) return true;
  forget_dead_reflections(ram);
  for (unsigned i = 0; i < KSX_ENEMY_SHOT_SLOTS; ++i) {
    const unsigned shot = KSX_ENEMY_SHOT_FIRST + i * 64;
    if (!ram[shot] || (ram[shot] & 0x40) || (st.reflected & (1u << i)) ||
        !MmxSaberAttackObjectBox(ram, shot, shot_box) ||
        shot_box[2] < saber[0] || shot_box[0] > saber[2] ||
        shot_box[3] < saber[1] || shot_box[1] > saber[3])
      continue;
    if (ram[shot + 0x28]) {
      /* A physical (destructible) shot is cut, as native $84:9BE5 does. */
      memset(ram + shot, 0, 4);
      putword(ram + shot + 0x0e, 0);
      putword(ram + shot + 0x2c, 0);
    } else {
      /* An energy shot is turned back the way it came. */
      putword(ram + shot + 0x1a, (0x10000u - word(ram + shot + 0x1a)) & 0xffff);
      putword(ram + shot + 0x1c, (0x10000u - word(ram + shot + 0x1c)) & 0xffff);
      ram[shot + 0x11] ^= 0x40;
      st.reflected |= (uint8_t)(1u << i);
      st.reflected_kind[i] = ram[shot + 0x0a];
    }
  }
  return true;
}

/* ---- Stagger Combos ----------------------------------------------------- */

static bool boss_slot(const uint8_t *ram, unsigned enemy) {
  return ram && enemy >= KSX_ENEMY_FIRST && enemy < 0x1228 &&
      (enemy & 63) == 0x28 && ram[enemy] &&
      MmxWidePolicy_IsBossEncounter(ram[enemy + 0x0a]);
}

static KsxStaggerSlot *slot_of(unsigned enemy) {
  if (enemy < KSX_ENEMY_FIRST || enemy >= 0x1228 || (enemy & 63) != 0x28)
    return NULL;
  return &st.slots[(enemy - KSX_ENEMY_FIRST) / 64];
}

static bool stagger_on(void) {
  return ksx_enabled && MmxSaberTuningStaggerCombos();
}

void MmxSaberKsxStaggerObserve(const uint8_t *ram) {
  st.restagger = false;
  if (!stagger_on() || !ram) return;
  for (unsigned i = 0; i < KSX_ENEMY_SLOTS; ++i) {
    const unsigned enemy = KSX_ENEMY_FIRST + i * 64;
    KsxStaggerSlot *slot = &st.slots[i];
    if (!boss_slot(ram, enemy) || !(ram[enemy + 0x27] & 0x7f)) {
      memset(slot, 0, sizeof(*slot));
      continue;
    }
    const uint8_t hp = ram[enemy + 0x27] & 0x7f;
    if (slot->kind != ram[enemy + 0x0a]) {
      memset(slot, 0, sizeof(*slot));
      slot->kind = ram[enemy + 0x0a];
      slot->prev_hp = hp;
    }
    if (slot->since_hit < 0xffff) ++slot->since_hit;
    if (slot->noted < 255) ++slot->noted;
    if (hp < slot->prev_hp && slot->noted > 2) {
      /* Another player's hit (X, Axl, the other Zero) staggers the boss:
       * a new combo with no KSX move used yet. */
      slot->active = 1;
      slot->used = 0;
      slot->since_hit = 0;
    }
    if (slot->active && slot->since_hit > KSX_STAGGER_WINDOW) slot->active = 0;
    slot->prev_hp = hp;
  }
}

void MmxSaberKsxStaggerNote(const uint8_t *ram, unsigned enemy,
                            unsigned move, bool fresh) {
  KsxStaggerSlot *slot = slot_of(enemy);
  if (!stagger_on() || !slot || !boss_slot(ram, enemy) ||
      move >= MMX_SABER_PRIORITY_CLASS_COUNT)
    return;
  if (fresh || !slot->active) {
    slot->active = 1;
    slot->used = 0;
  }
  slot->used |= 1u << move;
  slot->last_move = (uint8_t)move;
  slot->last_swing = MmxSaberAttackSwingId();
  slot->since_hit = 0;
  slot->noted = 0;
  slot->kind = ram[enemy + 0x0a];
}

unsigned MmxSaberKsxStaggerResponse(uint8_t *ram, unsigned enemy,
                                    unsigned projectile, unsigned original,
                                    unsigned priority_result) {
  MmxSaberPriorityClassification c;
  KsxStaggerSlot *slot = slot_of(enemy);
  st.pending = false;
  if (priority_result != original || original != 0 || !stagger_on() ||
      !slot || !slot->active || slot->since_hit > KSX_STAGGER_WINDOW ||
      !boss_slot(ram, enemy) || !(ram[enemy + 0x27] & 0x7f) ||
      !MmxSaberPriorityClassify(ram, projectile, &c) ||
      c.priority_class == MMX_SABER_PRIORITY_CLASS_NONE ||
      /* KSX Zero's saber moves and buster shots; not the X3 wave. */
      (!MmxSaberAttackOwnsProjectile(ram, projectile) &&
       !(c.priority_class >= MMX_SABER_PRIORITY_CLASS_CHARGE_SMALL &&
         c.priority_class <= MMX_SABER_PRIORITY_CLASS_MAX_SHOT2)))
    return priority_result;
  /* A move already used in this combo meets the boss's invincibility; the
   * hits of one multi-hit swing are the same move and each restaggers.
   * Buster shots are separate shots: a repeated class never restaggers. */
  if ((slot->used & (1u << c.priority_class)) &&
      !(MmxSaberAttackOwnsProjectile(ram, projectile) &&
        slot->last_move == c.priority_class &&
        slot->last_swing == MmxSaberAttackSwingId()))
    return priority_result;
  st.pending = true;
  st.pending_enemy = (uint16_t)enemy;
  st.pending_projectile = (uint16_t)projectile;
  st.pending_move = (uint8_t)c.priority_class;
  st.restagger = true;
  st.restagger_enemy = (uint16_t)enemy;
  st.restagger_projectile = (uint16_t)projectile;
  /* Sentinel 1 re-enters X1's positive-response path: the hit reaction
   * (the stagger) runs again. The damage seam supplies the damage. */
  return 1;
}

bool MmxSaberKsxStaggerConsume(const uint8_t *ram, unsigned enemy,
                               unsigned projectile, unsigned *move) {
  (void)ram;
  if (!st.pending || st.pending_enemy != enemy ||
      st.pending_projectile != projectile)
    return false;
  st.pending = false;
  if (move) *move = st.pending_move;
  return true;
}

size_t MmxSaberKsxStateSize(void) { return sizeof(st); }
void MmxSaberKsxStateSave(uint8_t *out) { memcpy(out, &st, sizeof(st)); }
void MmxSaberKsxStateLoad(const uint8_t *in) { memcpy(&st, in, sizeof(st)); }
