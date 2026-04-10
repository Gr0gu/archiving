#include <stdint.h>
#include <stdlib.h>

#define MIN_RUN 3

uint8_t* apply_rle(const uint8_t* input, size_t len, size_t* out_len) {
    if (!input || len == 0) return NULL;

    uint8_t* output = malloc(len * 2); // worst case
    if (!output) return NULL;

    size_t out_idx = 0;
    size_t i = 0;

    while (i < len) {
        size_t run_len = 1;

        // detect run
        while (i + run_len < len &&
               input[i + run_len] == input[i] &&
               run_len < 255) {
            run_len++;
        }

        if (run_len >= MIN_RUN) {
            // RUN BLOCK
            output[out_idx++] = 0;            // marker for run
            output[out_idx++] = (uint8_t)run_len;
            output[out_idx++] = input[i];
            i += run_len;
        } else {
            // LITERAL BLOCK
            size_t lit_start = i;
            size_t lit_len = 0;

            while (i < len && lit_len < 255) {
                // stop if a run starts
                size_t lookahead = 1;
                while (i + lookahead < len &&
                       input[i + lookahead] == input[i] &&
                       lookahead < MIN_RUN) {
                    lookahead++;
                }

                if (lookahead >= MIN_RUN)
                    break;

                i++;
                lit_len++;
            }

            output[out_idx++] = 1; // marker for literal
            output[out_idx++] = (uint8_t)lit_len;

            for (size_t j = 0; j < lit_len; j++) {
                output[out_idx++] = input[lit_start + j];
            }
        }
    }

    uint8_t* optimized = realloc(output, out_idx);
    *out_len = out_idx;
    return optimized ? optimized : output;
}

uint8_t* inverse_rle(const uint8_t* input, size_t len, size_t* out_len) {
    if (!input || len == 0) {
        *out_len = 0;
        return NULL;
    }

    // first pass: calculate size
    size_t total = 0;
    for (size_t i = 0; i < len;) {
        uint8_t type = input[i++];
        uint8_t count = input[i++];

        if (type == 0) {
            total += count;
            i++; // skip value
        } else {
            total += count;
            i += count;
        }
    }

    uint8_t* output = malloc(total);
    if (!output) {
        *out_len = 0;
        return NULL;
    }

    size_t out_idx = 0;

    for (size_t i = 0; i < len;) {
        uint8_t type = input[i++];
        uint8_t count = input[i++];

        if (type == 0) {
            uint8_t val = input[i++];
            for (uint8_t j = 0; j < count; j++) {
                output[out_idx++] = val;
            }
        } else {
            for (uint8_t j = 0; j < count; j++) {
                output[out_idx++] = input[i++];
            }
        }
    }

    *out_len = total;
    return output;
}
