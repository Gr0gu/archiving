/*
 * compresser.c — SUDO Archiver entry point.
 *
 * Compression pipeline  (per chunk): BWT → MTF → RLE → Huffman
 * Decompression pipeline (per chunk): inv-Huffman → inv-RLE → inv-MTF → inv-BWT
 *
 * Usage:
 *   ./sudo_archiver -c archive.sudo file1 file2 ...   compress
 *   ./sudo_archiver -x archive.sudo                   extract
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

/*
 * 100 KB chunks are a sweet spot:
 *   - BWT's O(n² log n) qsort stays fast enough to be usable.
 *   - Large enough for good compression ratios.
 * Increase if you want better ratios on large files (at the cost of speed).
 */
#define CHUNK_SIZE (400 * 1024)

/* ------------------------------------------------------------------ */
/* File format                                                         */
/* ------------------------------------------------------------------ */

/*
 * .sudo file layout
 * -----------------
 *  [uint32_t magic]          — SUDO_MAGIC (0x4F445553)
 *  [uint64_t num_chunks]     — number of compressed chunks that follow
 *  For each chunk:
 *    [uint64_t bwt_primary_index]      — needed by inverse_bwt
 *    [uint64_t original_chunk_size]    — bytes in this chunk before BWT
 *    [uint64_t compressed_chunk_size]  — bytes of huffman payload
 *    [uint8_t  data[compressed_chunk_size]]
 */
typedef struct {
  uint64_t bwt_primary_index;
  uint64_t original_chunk_size;
  uint64_t compressed_chunk_size;
} __attribute__((packed)) ChunkHeader;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

void free_buffer(uint8_t* buffer) {
  if (buffer)
    free(buffer);
}

/* ------------------------------------------------------------------ */
/* Compression                                                         */
/* ------------------------------------------------------------------ */

int compress_file_by_chunks(const char* input_filename,
                            const char* output_filename) {
  FILE* in_file  = fopen(input_filename,  "rb");
  FILE* out_file = fopen(output_filename, "wb");

  if (!in_file || !out_file) {
    fprintf(stderr, "Error: cannot open '%s' or '%s'\n",
            input_filename, output_filename);
    if (in_file)  fclose(in_file);
    if (out_file) fclose(out_file);
    return -1;
  }

  /* Write magic */
  uint32_t magic = SUDO_MAGIC;
  fwrite(&magic, sizeof(magic), 1, out_file);

  /* Write placeholder chunk count — we'll rewrite it at the end */
  uint64_t num_chunks = 0;
  long count_offset = ftell(out_file);   /* remember position */
  fwrite(&num_chunks, sizeof(num_chunks), 1, out_file);

  uint8_t* read_buffer = malloc(CHUNK_SIZE);
  if (!read_buffer) {
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  size_t bytes_read;
  while ((bytes_read = fread(read_buffer, 1, CHUNK_SIZE, in_file)) > 0) {

    /* ---- 1. BWT ---- */
    size_t primary_index = 0;
    uint8_t* bwt_out = apply_bwt(read_buffer, bytes_read, &primary_index);
    if (!bwt_out) {
      fprintf(stderr, "Error: BWT failed on chunk %llu\n",
              (unsigned long long)num_chunks);
      break;
    }

    /* ---- 2. MTF ---- */
    uint8_t* mtf_out = apply_mtf(bwt_out, bytes_read);
    free_buffer(bwt_out);
    if (!mtf_out) {
      fprintf(stderr, "Error: MTF failed on chunk %llu\n",
              (unsigned long long)num_chunks);
      break;
    }

    /* ---- 3. RLE ---- */
    size_t rle_len = 0;
    uint8_t* rle_out = apply_rle(mtf_out, bytes_read, &rle_len);
    free_buffer(mtf_out);
    if (!rle_out) {
      fprintf(stderr, "Error: RLE failed on chunk %llu\n",
              (unsigned long long)num_chunks);
      break;
    }

    /* ---- 4. Huffman ---- */
    size_t huff_len = 0;
    uint8_t* huff_out = apply_huffman(rle_out, rle_len, &huff_len);
    free_buffer(rle_out);
    if (!huff_out) {
      fprintf(stderr, "Error: Huffman failed on chunk %llu\n",
              (unsigned long long)num_chunks);
      break;
    }

    /* ---- Write chunk header + payload ---- */
    ChunkHeader ch;
    ch.bwt_primary_index    = (uint64_t)primary_index;
    ch.original_chunk_size  = (uint64_t)bytes_read;
    ch.compressed_chunk_size = (uint64_t)huff_len;

    fwrite(&ch,       sizeof(ch), 1,        out_file);
    fwrite(huff_out,  1,          huff_len, out_file);
    free_buffer(huff_out);

    printf("  chunk %llu: %zu → %zu bytes (%.1f%%)\n",
           (unsigned long long)num_chunks,
           bytes_read, huff_len,
           100.0 * (double)huff_len / (double)bytes_read);

    num_chunks++;
  }

  free(read_buffer);
  fclose(in_file);

  /* Rewrite the real chunk count */
  fseek(out_file, count_offset, SEEK_SET);
  fwrite(&num_chunks, sizeof(num_chunks), 1, out_file);
  fclose(out_file);

  printf("Compression complete: %llu chunk(s).\n",
         (unsigned long long)num_chunks);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Decompression                                                       */
/* ------------------------------------------------------------------ */

int decompress_file_by_chunks(const char* input_filename,
                              const char* output_filename) {
  FILE* in_file  = fopen(input_filename,  "rb");
  FILE* out_file = fopen(output_filename, "wb");

  if (!in_file || !out_file) {
    fprintf(stderr, "Error: cannot open '%s' or '%s'\n",
            input_filename, output_filename);
    if (in_file)  fclose(in_file);
    if (out_file) fclose(out_file);
    return -1;
  }

  /* Verify magic */
  uint32_t magic = 0;
  if (fread(&magic, sizeof(magic), 1, in_file) != 1 || magic != SUDO_MAGIC) {
    fprintf(stderr, "Error: '%s' is not a valid .sudo archive\n", input_filename);
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  uint64_t num_chunks = 0;
  if (fread(&num_chunks, sizeof(num_chunks), 1, in_file) != 1) {
    fprintf(stderr, "Error: failed to read chunk count\n");
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  printf("Decompressing %llu chunk(s)...\n", (unsigned long long)num_chunks);

  for (uint64_t c = 0; c < num_chunks; c++) {

    /* Read chunk header */
    ChunkHeader ch;
    if (fread(&ch, sizeof(ch), 1, in_file) != 1) {
      fprintf(stderr, "Error: truncated archive at chunk %llu\n",
              (unsigned long long)c);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    /* Read compressed payload */
    uint8_t* comp_data = malloc(ch.compressed_chunk_size);
    if (!comp_data ||
        fread(comp_data, 1, ch.compressed_chunk_size, in_file)
          != ch.compressed_chunk_size) {
      fprintf(stderr, "Error: failed to read chunk %llu data\n",
              (unsigned long long)c);
      free_buffer(comp_data);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    /* ---- 1. inverse Huffman ---- */
    size_t rle_len = 0;
    uint8_t* rle_out = inverse_huffman(comp_data, ch.compressed_chunk_size, &rle_len);
    free_buffer(comp_data);
    if (!rle_out) {
      fprintf(stderr, "Error: inverse_huffman failed at chunk %llu\n",
              (unsigned long long)c);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    /* ---- 2. inverse RLE ---- */
    size_t mtf_len = 0;
    uint8_t* mtf_out = inverse_rle(rle_out, rle_len, &mtf_len);
    free_buffer(rle_out);
    if (!mtf_out) {
      fprintf(stderr, "Error: inverse_rle failed at chunk %llu\n",
              (unsigned long long)c);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    /* ---- 3. inverse MTF ---- */
    uint8_t* bwt_out = inverse_mtf(mtf_out, mtf_len);
    free_buffer(mtf_out);
    if (!bwt_out) {
      fprintf(stderr, "Error: inverse_mtf failed at chunk %llu\n",
              (unsigned long long)c);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    /* ---- 4. inverse BWT ---- */
    uint8_t* orig = inverse_bwt(bwt_out, ch.original_chunk_size,
                                 ch.bwt_primary_index);
    free_buffer(bwt_out);
    if (!orig) {
      fprintf(stderr, "Error: inverse_bwt failed at chunk %llu\n",
              (unsigned long long)c);
      fclose(in_file);
      fclose(out_file);
      return -1;
    }

    fwrite(orig, 1, ch.original_chunk_size, out_file);
    free_buffer(orig);

    printf("  chunk %llu: decompressed %llu bytes\n",
           (unsigned long long)c,
           (unsigned long long)ch.original_chunk_size);
  }

  fclose(in_file);
  fclose(out_file);
  printf("Decompression complete.\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "================================================\n");
    fprintf(stderr, "SUDO Archiver - Team FAF-252\n");
    fprintf(stderr, "================================================\n");
    fprintf(stderr,
      "Usage (compress): ./sudo_archiver -c <archive.sudo> <file1> ...\n");
    fprintf(stderr,
      "Usage (extract):  ./sudo_archiver -x <archive.sudo>\n");
    return EXIT_FAILURE;
  }

  const char* flag        = argv[1];
  const char* target_sudo = argv[2];
  const char* temp_arc    = "temp_bundle.arc";

  /* ==================== COMPRESSION (-c) ==================== */
  if (strcmp(flag, "-c") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Error: specify at least one file to compress.\n");
      return EXIT_FAILURE;
    }

    printf("1. Bundling files into temporary archive...\n");
    char** input_files = &argv[3];
    int    num_files   = argc - 3;

    if (arc_create(temp_arc, input_files, num_files) != 0) {
      fprintf(stderr, "Error: failed to bundle files.\n");
      return EXIT_FAILURE;
    }

    printf("2. Compressing bundle (BWT → MTF → RLE → Huffman)...\n");
    if (compress_file_by_chunks(temp_arc, target_sudo) != 0) {
      fprintf(stderr, "Error: compression pipeline failed.\n");
      remove(temp_arc);
      return EXIT_FAILURE;
    }

    printf("3. Cleaning up...\n");
    remove(temp_arc);

    printf("\n✅ Success: '%s' created.\n", target_sudo);

  /* ==================== EXTRACTION (-x) ==================== */
  } else if (strcmp(flag, "-x") == 0) {

    printf("1. Decompressing '%s' (Huffman → RLE → MTF → BWT)...\n",
           target_sudo);
    if (decompress_file_by_chunks(target_sudo, temp_arc) != 0) {
      fprintf(stderr, "Error: decompression pipeline failed.\n");
      return EXIT_FAILURE;
    }

    printf("2. Extracting files from archive...\n");
    if (arc_extract(temp_arc) != 0) {
      fprintf(stderr, "Error: failed to extract files.\n");
      remove(temp_arc);
      return EXIT_FAILURE;
    }

    printf("3. Cleaning up...\n");
    remove(temp_arc);

    printf("\n✅ Success: files extracted from '%s'.\n", target_sudo);

  /* ==================== UNKNOWN FLAG ==================== */
  } else {
    fprintf(stderr,
            "Error: unknown flag '%s'. Use -c to compress or -x to extract.\n",
            flag);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
