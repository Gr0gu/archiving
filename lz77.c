#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_SIZE 4096
#define MAX_MATCH 255
#define MIN_MATCH 3
#define HASH_SIZE 4096

uint8_t* apply_lz77(const uint8_t* input, size_t len, size_t* out_len) {
  uint8_t* output = malloc(len * 2 + 16);
  uint16_t* head = calloc(HASH_SIZE, sizeof(uint16_t));
  uint16_t* prev = calloc(WINDOW_SIZE, sizeof(uint16_t));

  size_t in_pos = 0, out_pos = 0;

  while (in_pos < len) {
    uint16_t best_len = 0;
    uint16_t best_dist = 0;

    if (in_pos + MIN_MATCH <= len) {
      uint16_t h = ((input[in_pos] << 8) ^ (input[in_pos + 1] << 4) ^
                    input[in_pos + 2]) %
                   HASH_SIZE;
      uint16_t match_idx = head[h];

      int chain_limit = 64;
      while (match_idx > 0 && chain_limit--) {
        uint32_t dist = in_pos - (match_idx - 1);
        if (dist >= WINDOW_SIZE)
          break;

        uint16_t cur_len = 0;
        while (cur_len < MAX_MATCH && in_pos + cur_len < len &&
               input[in_pos + cur_len] == input[(match_idx - 1) + cur_len]) {
          cur_len++;
        }

        if (cur_len > best_len) {
          best_len = cur_len;
          best_dist = (uint16_t)dist;
        }
        match_idx = prev[(match_idx - 1) % WINDOW_SIZE];
      }

      prev[in_pos % WINDOW_SIZE] = head[h];
      head[h] = (uint16_t)(in_pos + 1);
    }

    if (best_len >= MIN_MATCH) {
      output[out_pos++] = 1;
      output[out_pos++] = (uint8_t)(best_dist & 0xFF);
      output[out_pos++] = (uint8_t)((best_dist >> 8) & 0xFF);
      output[out_pos++] = (uint8_t)best_len;
      in_pos += best_len;
    } else {
      output[out_pos++] = 0;
      output[out_pos++] = input[in_pos++];
    }
  }

  free(head);
  free(prev);
  *out_len = out_pos;
  return (int8_t*)output;
}

uint8_t* inverse_lz77(const uint8_t* input, size_t len, size_t* out_len) {
  size_t capacity = len * 2;
  uint8_t* output = malloc(capacity);
  size_t in_pos = 0, out_pos = 0;

  while (in_pos < len) {
    if (out_pos + 258 >= capacity) {
      capacity *= 2;
      output = realloc(output, capacity);
    }

    uint8_t tag = input[in_pos++];
    if (tag == 0) {
      output[out_pos++] = input[in_pos++];
    } else {
      uint16_t dist = input[in_pos++];
      dist |= (uint16_t)(input[in_pos++] << 8);
      uint8_t length = input[in_pos++];

      size_t start = out_pos - dist;
      for (int i = 0; i < length; i++) {
        output[out_pos++] = output[start + i];
      }
    }
  }

  *out_len = out_pos;
  return output;
}
