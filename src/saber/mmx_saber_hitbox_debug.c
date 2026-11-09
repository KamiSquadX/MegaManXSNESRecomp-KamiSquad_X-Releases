#include "mmx_saber_hitbox_debug.h"

#include "mmx_saber_attack.h"
#include "mmx_saber_ksx.h"
#include "mmx_saber_wave_runtime.h"
#include "../mmx_zero.h"

enum {
  MMX_SABER_DEBUG_PLAYER = 0x0ba8,
  MMX_SABER_DEBUG_ENEMY_FIRST = 0x0e68,
  MMX_SABER_DEBUG_ENEMY_END = 0x1228,
  MMX_SABER_DEBUG_PROJECTILE_FIRST = 0x1228,
  MMX_SABER_DEBUG_PROJECTILE_END = 0x1428,
  MMX_SABER_DEBUG_SLOT_BYTES = 0x40,
  MMX_SABER_DEBUG_COLLISION_BANK_OFFSET = 0x30000
};

static const uint8_t *debug_ram;
static const uint8_t *debug_rom;
static size_t debug_rom_size;

static unsigned word(const uint8_t *bytes, unsigned offset) {
  return bytes[offset] | ((unsigned)bytes[offset + 1] << 8);
}

static const uint8_t *collision_record(unsigned pointer) {
  size_t offset;
  if (!debug_rom || pointer < 0x8000) return NULL;
  offset = MMX_SABER_DEBUG_COLLISION_BANK_OFFSET + (pointer & 0x7fff);
  if (offset > debug_rom_size || debug_rom_size - offset < 4) return NULL;
  return debug_rom + offset;
}

static bool record_present(const uint8_t *record) {
  return record && !(record[0] == 0xff && record[1] == 0xff &&
                     record[2] == 0xff && record[3] == 0xff);
}

static unsigned append_box(MmxRenderDebugRect *out, unsigned max,
                           unsigned object, uint16_t color, unsigned count) {
  const uint8_t *record;
  int object_x, object_y;
  int center_x;

  if (count >= max || !debug_ram || !debug_ram[object]) return count;
  object_x = (int16_t)word(debug_ram, object + 5);
  object_y = (int16_t)word(debug_ram, object + 8);
  record = collision_record(word(debug_ram, object + 0x20));
  if (!record_present(record)) return count;
  center_x = (int8_t)record[0];
  if (debug_ram[object + 0x11] & 0x40) center_x = -center_x;
  if (out) {
    out[count].world_x = object_x + center_x - record[2];
    out[count].world_y = object_y + (int8_t)record[1] - record[3];
    out[count].w = (uint16_t)(record[2] * 2u + 1u);
    out[count].h = (uint16_t)(record[3] * 2u + 1u);
    out[count].rgb555 = color;
  }
  return count + 1;
}

static bool saber_tag(unsigned tag) {
  return tag == 0x5a53 ||
      (tag & MMX_SABER_PROJECTILE_TAG_FAMILY_MASK) ==
          MMX_SABER_PROJECTILE_TAG_FAMILY ||
      ((tag & MMX_SABER_WAVE_TAG_FAMILY_MASK) ==
           MMX_SABER_WAVE_TAG_FAMILY && (tag & 0xff) != 0);
}

void MmxSaberHitboxDebugSetRam(const uint8_t *ram) {
  debug_ram = ram;
}

void MmxSaberHitboxDebugSetRom(const uint8_t *rom, size_t size) {
  debug_rom = rom;
  debug_rom_size = size;
}

void MmxSaberHitboxDebugReset(void) {
  debug_ram = NULL;
  debug_rom = NULL;
  debug_rom_size = 0;
}

/* Rakuhouha's areas: the blast and every live shard, each its own box
 * (one live object takes them in turn, so the object shows only one). */
unsigned MmxSaberHitboxDebugKsx(MmxRenderDebugRect *out, unsigned max) {
  MmxSaberKsxGigaTarget targets[10];
  const MmxSaberAttackSnapshot s = MmxSaberAttackSnapshotGet();
  unsigned count = 0, n;
  int box[4];
  if (!debug_ram || !max) return 0;
  if (MmxSaberAttackKsxShownBox(box)) {
    /* The other KSX moves, from their last published box: co-op empties
     * the shared record on the other seat's turn. */
    if (out) {
      out[0].world_x = box[0];
      out[0].world_y = box[1];
      out[0].w = (uint16_t)(box[2] - box[0] + 1);
      out[0].h = (uint16_t)(box[3] - box[1] + 1);
      out[0].rgb555 = MMX_SABER_HITBOX_CYAN;
    }
    return 1;
  }
  if (s.kind != SABER_KIND_RAKUHOUHA || s.phase != SABER_PHASE_ACTIVE)
    return 0;
  n = MmxSaberKsxGigaTargets(debug_ram, s.tick, s.facing, targets, 10);
  for (unsigned i = 0; i < n && count < max; ++i, ++count) {
    if (!out) continue;
    out[count].world_x = targets[i].x - targets[i].half_w;
    out[count].world_y = targets[i].y - targets[i].half_h;
    out[count].w = (uint16_t)(targets[i].half_w * 2u + 1u);
    out[count].h = (uint16_t)(targets[i].half_h * 2u + 1u);
    out[count].rgb555 = MMX_SABER_HITBOX_CYAN;
  }
  return count;
}

unsigned MmxSaberHitboxDebugProvide(MmxRenderDebugRect *out, unsigned max) {
  unsigned count = 0;

  if (!debug_ram || !max) return 0;
  for (unsigned object = MMX_SABER_DEBUG_ENEMY_FIRST;
       object < MMX_SABER_DEBUG_ENEMY_END && count < max;
       object += MMX_SABER_DEBUG_SLOT_BYTES)
    count = append_box(out, max, object, MMX_SABER_HITBOX_RED, count);

  if (MmxZeroActive() && count < max)
    count = append_box(out, max, MMX_SABER_DEBUG_PLAYER,
                       MMX_SABER_HITBOX_GREEN, count);

  /* Keep Saber outlines over ordinary shots when objects overlap. */
  for (unsigned object = MMX_SABER_DEBUG_PROJECTILE_FIRST;
       object < MMX_SABER_DEBUG_PROJECTILE_END && count < max;
       object += MMX_SABER_DEBUG_SLOT_BYTES) {
    if (saber_tag(word(debug_ram, object + 0x3e))) continue;
    count = append_box(out, max, object, MMX_SABER_HITBOX_YELLOW, count);
  }
  for (unsigned object = MMX_SABER_DEBUG_PROJECTILE_FIRST;
       object < MMX_SABER_DEBUG_PROJECTILE_END && count < max;
       object += MMX_SABER_DEBUG_SLOT_BYTES) {
    if (!saber_tag(word(debug_ram, object + 0x3e))) continue;
    count = append_box(out, max, object, MMX_SABER_HITBOX_CYAN, count);
  }
  if (count < max) count += MmxSaberHitboxDebugKsx(out ? out + count : NULL, max - count);
  return count;
}
