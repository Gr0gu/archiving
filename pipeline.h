#ifndef PIPELINE_H
#define PIPELINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * 1. ARCHIVER / CONTAINER | Ion
 * ========================================================================= */

#define SUDO_MAGIC 0x4F445553

typedef struct {
  uint32_t magic;
  uint32_t original_size;
  uint32_t compressed_size;
  uint32_t crc32;
  char     filename[256];
} SudoHeader;

int arc_create(const char* archive_path, char** file_argv, int file_argc);
int arc_extract(const char* archive_path);

/* =========================================================================
 * 2. BURROWS-WHEELER TRANSFORM | Dragoș
 * ========================================================================= */

uint8_t* apply_bwt(const uint8_t* input, size_t len, size_t* out_primary_index);
uint8_t* inverse_bwt(const uint8_t* input, size_t len, size_t primary_index);

/* =========================================================================
 * 3. MOVE-TO-FRONT ENCODING | Chirill
 * ========================================================================= */

uint8_t* apply_mtf(const uint8_t* input, size_t len);
uint8_t* inverse_mtf(const uint8_t* input, size_t len);

/* =========================================================================
 * 4. RUN-LENGTH ENCODING | Călin
 * ========================================================================= */

uint8_t* apply_rle(const uint8_t* input, size_t len, size_t* out_len);
uint8_t* inverse_rle(const uint8_t* input, size_t len, size_t* out_len);

/* =========================================================================
 * 5. HUFFMAN CODING | Danu
 * ========================================================================= */

uint8_t* apply_huffman(const uint8_t* input, size_t len, size_t* out_len);
uint8_t* inverse_huffman(const uint8_t* input, size_t len, size_t* out_len);

/* =========================================================================
 * UTILITY
 * ========================================================================= */

void free_buffer(uint8_t* buffer);

#endif /* PIPELINE_H */
