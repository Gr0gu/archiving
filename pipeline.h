#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* =========================================================================
 * 1. ARCHIVER / CONTAINER | Ion
 * ========================================================================= */

// Magic bytes to identify a .sudo file (e.g., "SUDO")
#define SUDO_MAGIC 0x4F445553 

typedef struct {
    uint32_t magic;           // Must be SUDO_MAGIC
    uint32_t original_size;   // Size before compression
    uint32_t compressed_size; // Size after full pipeline
    uint32_t crc32;           // Checksum for integrity verification
    char filename[256];       // Original filename
} SudoHeader;

// Bundles files, calls the compression pipeline, and writes the .sudo file
int create_sudo_archive(const char* archive_name, const char** filenames, int num_files);

// Reads a .sudo file, runs the decompression pipeline, and restores files
int extract_sudo_archive(const char* archive_name);

// Calculates checksum for data integrity
uint32_t calculate_crc32(const uint8_t* data, size_t length);


/* =========================================================================
 * 2. BURROWS-WHEELER TRANSFORM | Dragoș
 * ========================================================================= */

// Note: BWT needs to return the 'primary index' (the original string's row) 
// for the inverse function to reconstruct the data.
uint8_t* apply_bwt(const uint8_t* input, size_t len, size_t* out_primary_index);
uint8_t* inverse_bwt(const uint8_t* input, size_t len, size_t primary_index);


/* =========================================================================
 * 3. MOVE-TO-FRONT ENCODING | Chirill
 * ========================================================================= */

// Transforms clustered characters into low-value integers (many zeros)
uint8_t* apply_mtf(const uint8_t* input, size_t len);
uint8_t* inverse_mtf(const uint8_t* input, size_t len);


/* =========================================================================
 * 4. RUN-LENGTH ENCODING | Călin
 * ========================================================================= */

// Compresses runs of identical bytes. Length will likely decrease here.
uint8_t* apply_rle(const uint8_t* input, size_t len, size_t* out_len);
uint8_t* inverse_rle(const uint8_t* input, size_t len, size_t* out_len);


/* =========================================================================
 * 5. LZ77 DICTIONARY COMPRESSION | Marius
 * ========================================================================= */

// Sliding window pattern matching. Replaces recurring patterns with (offset, length).
uint8_t* apply_lz77(const uint8_t* input, size_t len, size_t* out_len);
uint8_t* inverse_lz77(const uint8_t* input, size_t len, size_t* out_len);


/* =========================================================================
 * 6. HUFFMAN CODING | Danu
 * ========================================================================= */

// Entropy coding. Needs to serialize the Huffman tree/frequency table 
// into the output buffer so the decoder can rebuild it.
uint8_t* apply_huffman(const uint8_t* input, size_t len, size_t* out_len);
uint8_t* inverse_huffman(const uint8_t* input, size_t len, size_t* out_len);

/* =========================================================================
 * UTILITY FUNCTIONS
 * ========================================================================= */

// Helper to free memory allocated by the pipeline stages
void free_buffer(uint8_t* buffer);

#endif // PIPELINE_H