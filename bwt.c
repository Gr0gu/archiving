#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

static const uint8_t* bwt_input = NULL;
static size_t bwt_len = 0;

static int bwt_rotation_compare(const void* a, const void* b) {
  size_t ia = *(const size_t*)a;
  size_t ib = *(const size_t*)b;

  if (ia == ib)
    return 0;

  for (size_t k = 0; k < bwt_len; ++k) {
    uint8_t ca = bwt_input[(ia + k) % bwt_len];
    uint8_t cb = bwt_input[(ib + k) % bwt_len];

    if (ca < cb)
      return -1;
    if (ca > cb)
      return 1;
  }

  return 0;
}

uint8_t* apply_bwt(const uint8_t* input,
                   size_t len,
                   size_t* out_primary_index) {
  if (out_primary_index == NULL) {
    return NULL;
  }

  *out_primary_index = 0;

  if (len == 0) {
    return NULL;
  }

  uint8_t* output = (uint8_t*)malloc(len);
  if (output == NULL) {
    return NULL;
  }

  size_t* order = (size_t*)malloc(len * sizeof(size_t));
  if (order == NULL) {
    free(output);
    return NULL;
  }

  for (size_t i = 0; i < len; ++i) {
    order[i] = i;
  }

  bwt_input = input;
  bwt_len = len;
  qsort(order, len, sizeof(size_t), bwt_rotation_compare);

  for (size_t i = 0; i < len; ++i) {
    size_t rotation_start = order[i];
    if (rotation_start == 0) {
      *out_primary_index = i;
    }

    size_t last_index =
        (rotation_start == 0) ? (len - 1) : (rotation_start - 1);
    output[i] = input[last_index];
  }

  free(order);
  return output;
}

uint8_t* inverse_bwt(const uint8_t* input, size_t len, size_t primary_index) {
  if (len == 0) {
    return NULL;
  }

  if (primary_index >= len) {
    return NULL;
  }

  uint8_t* output = (uint8_t*)malloc(len);
  if (output == NULL) {
    return NULL;
  }

  size_t* rank = (size_t*)malloc(len * sizeof(size_t));
  size_t* next = (size_t*)malloc(len * sizeof(size_t));
  if (!rank || !next) {
    free(output);
    free(rank);
    free(next);
    return NULL;
  }

  size_t counts[256] = {0};

  for (size_t i = 0; i < len; ++i) {
    uint8_t c = input[i];
    rank[i] = counts[c];
    counts[c]++;
  }

  size_t starts[256] = {0};
  size_t total = 0;
  for (int c = 0; c < 256; ++c) {
    starts[c] = total;
    total += counts[c];
  }

  for (size_t i = 0; i < len; ++i) {
    uint8_t c = input[i];
    next[i] = starts[c] + rank[i];
  }

  size_t row = primary_index;
  for (size_t i = len; i > 0; --i) {
    output[i - 1] = input[row];
    row = next[row];
  }

  free(rank);
  free(next);
  return output;
}