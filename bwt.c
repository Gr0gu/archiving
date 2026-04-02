/*
 * bwt.c — Burrows-Wheeler Transform (forward and inverse).
 *
 * Key optimisation over the naive approach:
 *   Instead of comparing rotations via a character loop with modular
 *   arithmetic  (ia + k) % len,  we build a "doubled" buffer  [input | input]
 *   of length 2*len.  Rotation i then occupies the contiguous slice
 *   doubled[i .. i+len-1], so comparisons reduce to a single memcmp call.
 *   memcmp is SIMD-optimised by every modern libc, making this substantially
 *   faster than the hand-written byte loop for typical chunk sizes.
 */

#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

/* Used by the qsort comparator — not thread-safe, but we are single-threaded. */
static const uint8_t* g_doubled = NULL;
static size_t         g_len     = 0;

static int bwt_rotation_compare(const void* a, const void* b) {
  size_t ia = *(const size_t*)a;
  size_t ib = *(const size_t*)b;
  return memcmp(g_doubled + ia, g_doubled + ib, g_len);
}

uint8_t* apply_bwt(const uint8_t* input,
                   size_t len,
                   size_t* out_primary_index) {
  if (!out_primary_index)
    return NULL;

  *out_primary_index = 0;

  if (len == 0)
    return NULL;

  /*
   * doubled[0..2*len-1] = input ++ input
   * Rotation starting at index i is doubled[i..i+len-1] — no wrapping needed.
   */
  uint8_t* doubled = malloc(2 * len);
  if (!doubled)
    return NULL;
  memcpy(doubled,       input, len);
  memcpy(doubled + len, input, len);

  size_t* order = malloc(len * sizeof(size_t));
  if (!order) {
    free(doubled);
    return NULL;
  }

  for (size_t i = 0; i < len; ++i)
    order[i] = i;

  g_doubled = doubled;
  g_len     = len;
  qsort(order, len, sizeof(size_t), bwt_rotation_compare);
  g_doubled = NULL; /* clear after use */

  uint8_t* output = malloc(len);
  if (!output) {
    free(doubled);
    free(order);
    return NULL;
  }

  for (size_t i = 0; i < len; ++i) {
    if (order[i] == 0)
      *out_primary_index = i;
    /* Last character of rotation order[i] is the character just before it. */
    output[i] = input[order[i] > 0 ? order[i] - 1 : len - 1];
  }

  free(doubled);
  free(order);
  return output;
}

/*
 * inverse_bwt — standard LF-mapping reconstruction.
 *
 * Given the last column L (= input) and the primary index, rebuilds the
 * original string in O(n) time using rank/next arrays.
 */
uint8_t* inverse_bwt(const uint8_t* input, size_t len, size_t primary_index) {
  if (len == 0 || primary_index >= len)
    return NULL;

  uint8_t* output = malloc(len);
  size_t*  rank   = malloc(len * sizeof(size_t));
  size_t*  next   = malloc(len * sizeof(size_t));

  if (!output || !rank || !next) {
    free(output);
    free(rank);
    free(next);
    return NULL;
  }

  /* Count occurrences and assign per-symbol ranks. */
  size_t counts[256] = {0};
  for (size_t i = 0; i < len; ++i) {
    rank[i] = counts[input[i]]++;
  }

  /* Starting position of each symbol in the sorted (first) column. */
  size_t starts[256] = {0};
  size_t total = 0;
  for (int c = 0; c < 256; ++c) {
    starts[c] = total;
    total += counts[c];
  }

  /* next[i] = row in the sorted matrix that follows row i. */
  for (size_t i = 0; i < len; ++i)
    next[i] = starts[input[i]] + rank[i];

  /* Walk the LF-mapping backwards to reconstruct the original string. */
  size_t row = primary_index;
  for (size_t i = len; i > 0; --i) {
    output[i - 1] = input[row];
    row = next[row];
  }

  free(rank);
  free(next);
  return output;
}
