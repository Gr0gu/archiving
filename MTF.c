#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

uint8_t* apply_mtf(const uint8_t* input, size_t len) {
    if (input == NULL || len == 0) {
        return NULL;
    }

    uint8_t* output = (uint8_t*)malloc(len * sizeof(uint8_t));
    if (output == NULL) {
        return NULL;
    }

    uint8_t table[256];
    for (int i = 0; i < 256; i++) {
        table[i] = (uint8_t)i;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t symbol = input[i];
        uint8_t pos = 0;

        while (table[pos] != symbol) {
            pos++;
        }

        output[i] = pos;

        for (int j = pos; j > 0; j--) {
            table[j] = table[j - 1];
        }
        table[0] = symbol;
    }

    return output;
}

uint8_t* inverse_mtf(const uint8_t* input, size_t len) {
    if (input == NULL || len == 0) {
        return NULL;
    }

    uint8_t* output = (uint8_t*)malloc(len * sizeof(uint8_t));
    if (output == NULL) {
        return NULL;
    }

    uint8_t table[256];
    for (int i = 0; i < 256; i++) {
        table[i] = (uint8_t)i;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t pos = input[i];
        uint8_t symbol = table[pos];

        output[i] = symbol;

        for (int j = pos; j > 0; j--) {
            table[j] = table[j - 1];
        }
        table[0] = symbol;
    }

    return output;
}
