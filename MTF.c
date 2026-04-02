/*
 * MTF.c — Move-To-Front encoding and decoding.
 *
 * Improvement over original:
 *   apply_mtf previously found each symbol's position via a linear scan
 *   (while table[pos] != symbol) — up to 256 iterations per byte.
 *   We now maintain a parallel inverse table  pos_of[symbol] = position,
 *   so the lookup is O(1).  The subsequent shift of table[0..pos-1] is
 *   unavoidable, but eliminating the scan roughly halves the work per byte
 *   for typical (non-clustered) input.
 *
 *   inverse_mtf already indexed by position directly (no scan needed),
 *   so its structure is unchanged apart from a minor cleanup.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t* apply_mtf(const uint8_t* input, size_t len) {
  if (!input || len == 0)
    return NULL;

  uint8_t* output = malloc(len);
  if (!output)
    return NULL;

  uint8_t table[256];  /* table[pos]    = symbol at that position */
  uint8_t pos_of[256]; /* pos_of[sym]   = current position of symbol */

  for (int i = 0; i < 256; i++) {
    table[i]  = (uint8_t)i;
    pos_of[i] = (uint8_t)i;
  }

  for (size_t i = 0; i < len; i++) {
    uint8_t sym = input[i];
    uint8_t pos = pos_of[sym]; /* O(1) lookup — no linear scan */

    output[i] = pos;

    /* Shift table[0..pos-1] one slot right, updating pos_of as we go. */
    for (int j = pos; j > 0; j--) {
      table[j]        = table[j - 1];
      pos_of[table[j]] = (uint8_t)j;
    }
    table[0]   = sym;
    pos_of[sym] = 0;
  }

  return output;
}

uint8_t* inverse_mtf(const uint8_t* input, size_t len) {
  if (!input || len == 0)
    return NULL;

  uint8_t* output = malloc(len);
  if (!output)
    return NULL;

  uint8_t table[256];
  for (int i = 0; i < 256; i++)
    table[i] = (uint8_t)i;

  for (size_t i = 0; i < len; i++) {
    uint8_t pos = input[i];
    uint8_t sym = table[pos]; /* direct index — no scan needed */

    output[i] = sym;

    /* Shift table[0..pos-1] one slot right. */
    for (int j = pos; j > 0; j--)
      table[j] = table[j - 1];
    table[0] = sym;
  }

  return output;
}
