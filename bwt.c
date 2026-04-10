#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* * Context structure to pass data to the comparator without using 
 * thread-unsafe global variables. 
 */
typedef struct {
    const uint8_t* doubled;
    size_t len;
} BWTContext;

/* * Thread-safe comparator. 
 * Note: The argument order for qsort_r varies between macOS/BSD and GNU/Linux.
 * This version uses the macOS/BSD/ARM64 standard.
 */
static int bwt_compare_safe(void* thunk, const void* a, const void* b) {
    BWTContext* ctx = (BWTContext*)thunk;
    uint32_t ia = *(const uint32_t*)a;
    uint32_t ib = *(const uint32_t*)b;
    
    /* Using memcmp on the doubled buffer is O(n) but heavily SIMD optimized */
    return memcmp(ctx->doubled + ia, ctx->doubled + ib, ctx->len);
}

uint8_t* apply_bwt(const uint8_t* input, size_t len, size_t* out_primary_index) {
    if (!input || len == 0 || !out_primary_index) return NULL;

    /* 1. Allocate memory for doubled buffer, suffix array, and output */
    uint8_t* doubled = malloc(2 * len);
    uint32_t* sa = malloc(len * sizeof(uint32_t));
    uint8_t* output = malloc(len);

    if (!doubled || !sa || !output) {
        free(doubled); free(sa); free(output);
        return NULL;
    }

    /* 2. Create the [input][input] buffer to eliminate modular arithmetic */
    memcpy(doubled, input, len);
    memcpy(doubled + len, input, len);

    /* 3. Initialize indices for the suffix array */
    for (uint32_t i = 0; i < (uint32_t)len; i++) {
        sa[i] = i;
    }

    /* 4. Sort indices using the thread-safe qsort_r */
    BWTContext ctx = { doubled, len };
    qsort_r(sa, len, sizeof(uint32_t), &ctx, bwt_compare_safe);

    /* 5. Construct the BWT output from the sorted suffix array */
    for (size_t i = 0; i < len; i++) {
        if (sa[i] == 0) {
            *out_primary_index = i;
            output[i] = input[len - 1];
        } else {
            output[i] = input[sa[i] - 1];
        }
    }

    free(doubled);
    free(sa);
    return output;
}

uint8_t* inverse_bwt(const uint8_t* input, size_t len, size_t primary_index) {
    if (!input || len == 0 || primary_index >= len) return NULL;

    uint8_t* output = malloc(len);
    uint32_t* next = malloc(len * sizeof(uint32_t));
    if (!output || !next) {
        free(output); free(next);
        return NULL;
    }

    /* LF-mapping reconstruction */
    size_t buckets[257] = {0};

    /* Pass 1: Frequency count */
    for (size_t i = 0; i < len; i++) {
        buckets[input[i] + 1]++;
    }

    /* Pass 2: Cumulative offsets */
    for (int i = 0; i < 256; i++) {
        buckets[i + 1] += buckets[i];
    }

    /* Pass 3: Threading the next array */
    for (size_t i = 0; i < len; i++) {
        next[buckets[input[i]]++] = (uint32_t)i;
    }

    /* Pass 4: Walk the mapping to reconstruct original bytes */
    uint32_t curr = next[primary_index];
    for (size_t i = 0; i < len; i++) {
        output[i] = input[curr];
        curr = next[curr];
    }

    free(next);
    return output;
}
