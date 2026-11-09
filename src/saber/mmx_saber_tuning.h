#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MmxSaberTuningDamageKind {
  MMX_SABER_TUNING_DAMAGE_SLASH1 = 0,
  MMX_SABER_TUNING_DAMAGE_SLASH2,
  MMX_SABER_TUNING_DAMAGE_SLASH3,
  MMX_SABER_TUNING_DAMAGE_X3_FINISHER,
  MMX_SABER_TUNING_DAMAGE_AIR,
  MMX_SABER_TUNING_DAMAGE_WALL,
  MMX_SABER_TUNING_DAMAGE_DASH,
  MMX_SABER_TUNING_DAMAGE_WAVE,
  /* KSX Zero's Special Moves and Giga Attack (KSX Zero settings only). */
  MMX_SABER_TUNING_DAMAGE_RYUENJIN,
  MMX_SABER_TUNING_DAMAGE_RAKUHOUHA,
  MMX_SABER_TUNING_DAMAGE_RAIKOUSEN,
  MMX_SABER_TUNING_DAMAGE_KUUENBU,
  MMX_SABER_TUNING_DAMAGE_HYOURETSUZAN,
  MMX_SABER_TUNING_DAMAGE_YOUDANTOTSU,
  MMX_SABER_TUNING_DAMAGE_JUUHAZAN,
  MMX_SABER_TUNING_DAMAGE_COUNT
} MmxSaberTuningDamageKind;

typedef enum MmxSaberTuningPriorityKind {
  MMX_SABER_TUNING_PRIORITY_SLASH1 = 0,
  MMX_SABER_TUNING_PRIORITY_SLASH2,
  MMX_SABER_TUNING_PRIORITY_SLASH3,
  MMX_SABER_TUNING_PRIORITY_AIR,
  MMX_SABER_TUNING_PRIORITY_WALL,
  MMX_SABER_TUNING_PRIORITY_DASH,
  MMX_SABER_TUNING_PRIORITY_CHARGE_SMALL,
  MMX_SABER_TUNING_PRIORITY_CHARGE_FULL,
  MMX_SABER_TUNING_PRIORITY_MAX_SHOT1,
  MMX_SABER_TUNING_PRIORITY_MAX_SHOT2,
  MMX_SABER_TUNING_PRIORITY_X3_FINISHER,
  MMX_SABER_TUNING_PRIORITY_WAVE,
  MMX_SABER_TUNING_PRIORITY_RYUENJIN,
  MMX_SABER_TUNING_PRIORITY_RAKUHOUHA,
  MMX_SABER_TUNING_PRIORITY_RAIKOUSEN,
  MMX_SABER_TUNING_PRIORITY_KUUENBU,
  MMX_SABER_TUNING_PRIORITY_HYOURETSUZAN,
  MMX_SABER_TUNING_PRIORITY_YOUDANTOTSU,
  MMX_SABER_TUNING_PRIORITY_JUUHAZAN,
  MMX_SABER_TUNING_PRIORITY_COUNT
} MmxSaberTuningPriorityKind;

typedef struct MmxSaberTuning {
  int normal_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
  int boss_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
  int priority[MMX_SABER_TUNING_PRIORITY_COUNT];
  int priority_window_frames;
  int finisher_window_frames;
  int saber_swing_volume;
  bool show_hitboxes;
  bool start_all_upgrades;
  bool stagger_combos;
  int rakuhouha_energy;
  /* Black Zero (all four armor parts): damage in place of the normal value
   * plus the arm parts' bonus. -1 keeps that derived value (normal + 2,
   * boss + 1), as the Saber Zero settings have no Black Zero view. */
  int black_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
  int black_boss_damage[MMX_SABER_TUNING_DAMAGE_COUNT];
} MmxSaberTuning;

/* The reader returns true when it supplied a value for option_id. */
typedef bool (*MmxSaberTuningOptionReader)(const char *option_id,
                                           char *value, size_t value_size,
                                           void *context);

/* Read all manifest tuning options into the Saber-owned cache. A NULL reader
 * resets the cache to the manifest defaults. */
void MmxSaberTuningLoad(MmxSaberTuningOptionReader reader, void *context);

const MmxSaberTuning *MmxSaberTuningGet(void);
int MmxSaberTuningNormalDamage(MmxSaberTuningDamageKind kind);
int MmxSaberTuningBossDamage(MmxSaberTuningDamageKind kind);
int MmxSaberTuningPriority(MmxSaberTuningPriorityKind kind);
int MmxSaberTuningPriorityWindowFrames(void);
int MmxSaberTuningFinisherWindowFrames(void);
int MmxSaberTuningSaberSwingVolume(void);
bool MmxSaberTuningShowHitboxes(void);
bool MmxSaberTuningStartAllUpgrades(void);
/* KSX Zero settings' Stagger Combos (default on). */
bool MmxSaberTuningStaggerCombos(void);
/* Weapon energy (of 28) one Rakuhouha spends. */
int MmxSaberTuningRakuhouhaEnergy(void);
/* A hit's damage before clamping: Black Zero's own value, else the normal or
 * boss value plus the arm parts' bonus (+2, +1 against bosses). */
int MmxSaberTuningArmoredDamage(MmxSaberTuningDamageKind kind, bool boss,
                                bool arms, bool black);

/* Short name for call sites that already establish the Saber context. */
int MmxSaberTuningSwingVolume(void);

#ifdef __cplusplus
}
#endif
