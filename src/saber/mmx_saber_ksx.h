#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mmx_saber_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* KSX Zero: Saber Zero as the base, with the Y saber, the buster on R2, and
 * Special Moves (X) and a Giga Attack (L2) learned from the X1 Mavericks
 * instead of their special weapons. A skill is learned when its Maverick's
 * weapon is owned ($7E:1F88 + 2n, bit $40), so passwords carry it too. */
typedef enum MmxKsxSkill {
  MMX_KSX_RYUENJIN,     /* Up + X           Flame Mammoth   (Fire Wave)      */
  MMX_KSX_RAKUHOUHA,       /* L2               Sting Chameleon (Chameleon Sting) */
  MMX_KSX_RAIKOUSEN,    /* Dash + X         Spark Mandrill  (Electric Spark) */
  MMX_KSX_KUUENBU,      /* X in the air     Armored Armadillo (Rolling Shield) */
  MMX_KSX_HYOURETSUZAN, /* Down + X, air    Chill Penguin   (Shotgun Ice)    */
  MMX_KSX_YOUDANTOTSU,  /* Forward + X      Launch Octopus  (Homing Torpedo) */
  MMX_KSX_JUUHAZAN,     /* X standing still Storm Eagle     (Storm Tornado)  */
  MMX_KSX_REKKYOUDAN,   /* passive          Boomer Kuwanger (Boomerang Cutter) */
  MMX_KSX_SKILL_COUNT
} MmxKsxSkill;

void MmxSaberKsxSetEnabled(bool enabled);
bool MmxSaberKsxEnabled(void);
/* X1 menu weapon (1 Homing Torpedo .. 8 Shotgun Ice) that teaches a skill. */
unsigned MmxSaberKsxSkillWeapon(MmxKsxSkill skill);
bool MmxSaberKsxLearned(const uint8_t *ram, MmxKsxSkill skill);
/* Pause-menu label of the skill a menu weapon slot teaches ("" if none). */
const char *MmxSaberKsxSkillName(MmxKsxSkill skill);
/* Its input command for the pause menu ("UP + SHOT 2"). */
const char *MmxSaberKsxSkillCommand(MmxKsxSkill skill);
MmxKsxSkill MmxSaberKsxSkillForWeapon(unsigned weapon);
/* The skill a KSX attack kind is (MMX_KSX_SKILL_COUNT for plain slashes). */
MmxKsxSkill MmxSaberKsxSkillForKind(MmxSaberPadKind kind);

typedef struct MmxSaberKsxInput {
  uint16_t pad;     /* the seat's SNES joypad word */
  uint8_t ext;      /* the seat's mapped extended actions (MMX_EXT_*) */
  bool grounded, wall, dash;
  uint8_t facing;   /* native facing ($40 = right) */
} MmxSaberKsxInput;

/* Pre-player: decide whether X / L2 starts a move this frame. Call after
 * the Saber attack step. */
void MmxSaberKsxPrePlayer(uint8_t *ram, MmxSaberKsxInput input);
/* Whether the KSX button is held (from the last PrePlayer). */
bool MmxSaberKsxHeld(void);
/* Pre-native and post-native motion for the running move. */
void MmxSaberKsxMotionPre(uint8_t *ram);
void MmxSaberKsxMotionPost(uint8_t *ram);
void MmxSaberKsxReset(void);
void MmxSaberKsxSetTerrainQuery(bool (*solid)(const uint8_t *ram, int x, int y));

/* Contact class for a KSX saber hit (MmxWeaponsContactClass override): a
 * move's own element where it is the target's weakness, else Boomerang
 * Cutter's once Rekkyoudan is learned (it cuts limbs), else unchanged. */
unsigned MmxSaberKsxContactClass(const uint8_t *ram, unsigned enemy,
                                 unsigned projectile, unsigned original,
                                 bool reaction);
/* X1's damage for a KSX Zero hit the target is weak to (a skill against
 * its Maverick's victim, or a Rekkyoudan slash against Sting Chameleon);
 * 0 when it is not a weakness. */
unsigned MmxSaberKsxWeaknessDamage(const uint8_t *ram, unsigned enemy,
                                   unsigned projectile);
/* Rekkyoudan learned: KSX Zero's saber is Black Zero's purple. */
bool MmxSaberKsxPurpleSaber(void);

/* Stagger Combos. Observe boss hits once per frame (any player's). */
void MmxSaberKsxStaggerObserve(const uint8_t *ram);
/* Response seam after the priority ladder: may admit a fresh move into a
 * staggered boss. Returns the response to use. */
unsigned MmxSaberKsxStaggerResponse(uint8_t *ram, unsigned enemy,
                                    unsigned projectile, unsigned original,
                                    unsigned priority_result);
/* Damage seam: true when this contact is an admitted restagger; *move is
 * the priority class that landed. */
bool MmxSaberKsxStaggerConsume(const uint8_t *ram, unsigned enemy,
                               unsigned projectile, unsigned *move);
/* A KSX-owned hit landed on `enemy` (fresh: through the native positive
 * path, which starts a new combo). */
void MmxSaberKsxStaggerNote(const uint8_t *ram, unsigned enemy,
                            unsigned move, bool fresh);

size_t MmxSaberKsxStateSize(void);
void MmxSaberKsxStateSave(uint8_t *out);
void MmxSaberKsxStateLoad(const uint8_t *in);

/* Rakuhouha's live shards for the renderer (visual only). angle n points
 * 22.5 * n degrees counter-clockwise from right. */
typedef struct MmxSaberKsxShard {
  int16_t world_x, world_y;
  uint8_t angle, age;
} MmxSaberKsxShard;
unsigned MmxSaberKsxShards(MmxSaberKsxShard *out, unsigned max);
/* Advance and spawn shards; runs every gameplay frame, hurt or not. */
void MmxSaberKsxShardsStep(const uint8_t *ram);

/* Native $84:9BC8 (a player shot against X1's enemy shots) for a KSX
 * saber hit. True when KSX handled it and native must be skipped: without
 * Rekkyoudan nothing is cut; with it, physical shots are cut and energy
 * shots are turned back. */
bool MmxSaberKsxEnemyShotHit(uint8_t *ram, unsigned projectile);

/* KSX Zero's weapon: the buster or Rakuhouha, kept in KSX's own state (X1's
 * $0BDB stays on the buster, as with the X2/X3 packs). */
bool MmxSaberKsxRakuhouhaSelected(void);
/* L/R pressed this frame ($0BE2 bits $30, consumed by the caller). */
void MmxSaberKsxShoulders(uint8_t *ram, unsigned pressed);
/* The pause menu closed on X1 menu item `item`. */
void MmxSaberKsxMenuChoice(uint8_t *ram, unsigned item);
/* Zero's player end: weapon upkeep, the guard's blink and Rakuhouha's
 * block sound. */
void MmxSaberKsxPlayerEnd(uint8_t *ram);

typedef struct MmxSaberKsxGigaTarget {
  int16_t x, y;            /* world centre */
  uint8_t half_w, half_h;
} MmxSaberKsxGigaTarget;
/* Rakuhouha's hit areas on attack tick `tick`: the blast around Zero, then
 * each live shard. */
unsigned MmxSaberKsxGigaTargets(const uint8_t *ram, unsigned tick,
                                uint8_t facing, MmxSaberKsxGigaTarget *out,
                                unsigned max);

#ifdef __cplusplus
}
#endif
