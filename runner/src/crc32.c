#include "crc32.h"
#include <string.h>

/* Slicing-by-8: the same IEEE CRC32 as the byte-at-a-time loop (identical
 * results for every input), eight bytes per step. The netplay state digest
 * folds roughly 200 KB through here every frame; byte-at-a-time that was
 * ~0.8 ms of each rollback frame. */
static uint32_t s_table[8][256];
static int      s_table_ready = 0;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++)
            s_table[t][i] = (s_table[t - 1][i] >> 8) ^ s_table[0][s_table[t - 1][i] & 0xFF];
    s_table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!s_table_ready) crc32_init_table();
    while (len && ((uintptr_t)data & 7)) {
        crc = s_table[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
        --len;
    }
    while (len >= 8) {
        /* Little-endian loads (every target this runner builds for). */
        uint32_t lo, hi;
        memcpy(&lo, data, 4);
        memcpy(&hi, data + 4, 4);
        lo ^= crc;
        crc = s_table[7][lo & 0xFF] ^ s_table[6][(lo >> 8) & 0xFF] ^
              s_table[5][(lo >> 16) & 0xFF] ^ s_table[4][lo >> 24] ^
              s_table[3][hi & 0xFF] ^ s_table[2][(hi >> 8) & 0xFF] ^
              s_table[1][(hi >> 16) & 0xFF] ^ s_table[0][hi >> 24];
        data += 8;
        len -= 8;
    }
    while (len--)
        crc = s_table[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t crc32_compute(const uint8_t *data, size_t len) {
    return crc32_final(crc32_update(CRC32_INIT, data, len));
}
