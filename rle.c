#include <stdint.h>
#include <stdlib.h>

uint8_t* apply_rle(const uint8_t* input, size_t len, size_t* out_len) {
    if (!input || len == 0) return NULL;
 
    uint8_t* output = malloc(len * 2);
    if (!output) return NULL;

    size_t out_idx = 0;
    for (size_t i = 0; i < len; ) {
        uint8_t count = 1;
        uint8_t val = input[i];
         
        while (i + count < len && input[i + count] == val && count < 255) {
            count++;
        }

        output[out_idx++] = count;
        output[out_idx++] = val;
        i += count;
    }

    uint8_t* optimized_buf = realloc(output, out_idx);
    *out_len = out_idx;

    return optimized_buf ? optimized_buf : output;
}

uint8_t* inverse_rle(const uint8_t* input, size_t len, size_t* out_len) {
    if (input == NULL || len == 0 || len % 2 != 0) {
        *out_len = 0;
        return NULL;
    }

    size_t total_size = 0;
    for (size_t i = 0; i < len; i += 2) {
        total_size += input[i];
    }

    uint8_t* output = (uint8_t*)malloc(total_size);
    if (output == NULL) {
        *out_len = 0;
        return NULL;
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < len; i += 2) {
        uint8_t count = input[i];
        uint8_t value = input[i + 1];


        for (uint8_t j = 0; j < count; j++) {
            output[out_idx++] = value;
        }
    }

    *out_len = total_size;
    return output;
}
