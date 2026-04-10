/*
 * compresser.c — SUDO Archiver entry point.
 *
 * Compression pipeline  (per chunk, level-dependent):
 *   Level 1: Huffman
 *   Level 2: RLE → Huffman
 *   Level 3: MTF → RLE → Huffman
 *   Level 4: BWT → MTF → RLE → Huffman  (default)
 *   Level 5: BWT → MTF → RLE → Huffman  (larger chunks, best ratio)
 *
 * Optional XOR encryption is applied after compression and stripped
 * before decompression when a password is provided.
 *
 * Usage:
 *   ./sudo_archiver -c [-l <1-5>] [-p <password>] archive.sudo file1 ...
 *   ./sudo_archiver -x [-p <password>] archive.sudo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

/* ------------------------------------------------------------------ */
/* File format                                                         */
/* ------------------------------------------------------------------ */

/*
 * .sudo file layout
 * -----------------
 *  [uint32_t magic]          — SUDO_MAGIC (0x4F445553)
 *  [uint8_t  level]          — compression level used (1–5)
 *  [uint8_t  encrypted]      — 1 if XOR-encrypted, 0 otherwise
 *  [uint8_t  reserved[2]]    — padding / future use
 *  [uint64_t num_chunks]     — number of compressed chunks that follow
 *  For each chunk:
 *    [uint64_t bwt_primary_index]      — needed by inverse_bwt (0 if unused)
 *    [uint64_t original_chunk_size]    — bytes in this chunk before any transform
 *    [uint64_t compressed_chunk_size]  — bytes of payload that follow
 *    [uint8_t  data[compressed_chunk_size]]
 */
typedef struct {
  uint32_t magic;
  uint8_t  level;
  uint8_t  encrypted;
  uint8_t  reserved[2];
  uint64_t num_chunks;
} __attribute__((packed)) FileHeader;

typedef struct {
  uint64_t bwt_primary_index;
  uint64_t original_chunk_size;
  uint64_t compressed_chunk_size;
} __attribute__((packed)) ChunkHeader;

/* ------------------------------------------------------------------ */
/* Chunk size per level                                                */
/* ------------------------------------------------------------------ */

/*
 * Larger chunks give the BWT more context, improving compression ratios
 * at the cost of speed and memory.  Levels without BWT use smaller chunks
 * since they benefit less from block size increases.
 */
static size_t chunk_size_for_level(int level) {
  switch (level) {
    case 1: return  50 * 1024;
    case 2: return 100 * 1024;
    case 3: return 200 * 1024;
    case 5: return 800 * 1024;
    default: return 400 * 1024; /* level 4 and fallback */
  }
}

/* ------------------------------------------------------------------ */
/* XOR encryption / decryption                                        */
/* ------------------------------------------------------------------ */

/*
 * Simple XOR stream cipher: each byte is XOR-ed with the corresponding
 * byte of the password (cycling when the password is shorter than data).
 * Encryption and decryption are the same operation.
 *
 * Note: this provides basic obfuscation, not cryptographic security.
 */
static void xor_crypt(uint8_t* data, size_t len, const char* password) {
  size_t keylen = strlen(password);
  if (keylen == 0)
    return;
  for (size_t i = 0; i < len; i++)
    data[i] ^= (uint8_t)password[i % keylen];
}

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

/*
 * compress_file_by_chunks — read input in fixed-size chunks and compress
 * each one through the level-appropriate pipeline.  If password is non-NULL
 * and non-empty, the compressed payload of every chunk is XOR-encrypted
 * before being written to the output file.
 */
int compress_file_by_chunks(const char* input_filename,
                            const char* output_filename,
                            int         level,
                            const char* password) {
  FILE* in_file  = fopen(input_filename,  "rb");
  FILE* out_file = fopen(output_filename, "wb");

  if (!in_file || !out_file) {
    fprintf(stderr, "Error: cannot open '%s' or '%s'\n",
            input_filename, output_filename);
    if (in_file)  fclose(in_file);
    if (out_file) fclose(out_file);
    return -1;
  }

  int use_encryption = (password && password[0] != '\0');

  /* Write file header */
  FileHeader fhdr;
  fhdr.magic     = SUDO_MAGIC;
  fhdr.level     = (uint8_t)level;
  fhdr.encrypted = (uint8_t)use_encryption;
  fhdr.reserved[0] = 0;
  fhdr.reserved[1] = 0;
  fhdr.num_chunks  = 0;         /* placeholder — rewritten at the end */
  long hdr_offset = 0;          /* FileHeader starts at byte 0 */
  fwrite(&fhdr, sizeof(fhdr), 1, out_file);

  size_t chunk_size = chunk_size_for_level(level);
  uint8_t* read_buffer = malloc(chunk_size);
  if (!read_buffer) {
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  uint64_t num_chunks = 0;
  size_t bytes_read;

  while ((bytes_read = fread(read_buffer, 1, chunk_size, in_file)) > 0) {

    size_t   primary_index = 0;
    uint8_t* final_out     = NULL;
    size_t   final_len     = 0;

    /* ---- Apply the pipeline for the chosen level ---- */
    switch (level) {

      /* Level 1: Huffman only */
      case 1: {
        final_out = apply_huffman(read_buffer, bytes_read, &final_len);
        if (!final_out) {
          fprintf(stderr, "Error: Huffman failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        break;
      }

      /* Level 2: RLE → Huffman */
      case 2: {
        size_t rle_len = 0;
        uint8_t* rle_out = apply_rle(read_buffer, bytes_read, &rle_len);
        if (!rle_out) {
          fprintf(stderr, "Error: RLE failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        final_out = apply_huffman(rle_out, rle_len, &final_len);
        free_buffer(rle_out);
        if (!final_out) {
          fprintf(stderr, "Error: Huffman failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        break;
      }

      /* Level 3: MTF → RLE → Huffman */
      case 3: {
        uint8_t* mtf_out = apply_mtf(read_buffer, bytes_read);
        if (!mtf_out) {
          fprintf(stderr, "Error: MTF failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        size_t rle_len = 0;
        uint8_t* rle_out = apply_rle(mtf_out, bytes_read, &rle_len);
        free_buffer(mtf_out);
        if (!rle_out) {
          fprintf(stderr, "Error: RLE failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        final_out = apply_huffman(rle_out, rle_len, &final_len);
        free_buffer(rle_out);
        if (!final_out) {
          fprintf(stderr, "Error: Huffman failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        break;
      }

      /* Level 4 & 5: BWT → MTF → RLE → Huffman */
      default: {
        uint8_t* bwt_out = apply_bwt(read_buffer, bytes_read, &primary_index);
        if (!bwt_out) {
          fprintf(stderr, "Error: BWT failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        uint8_t* mtf_out = apply_mtf(bwt_out, bytes_read);
        free_buffer(bwt_out);
        if (!mtf_out) {
          fprintf(stderr, "Error: MTF failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        size_t rle_len = 0;
        uint8_t* rle_out = apply_rle(mtf_out, bytes_read, &rle_len);
        free_buffer(mtf_out);
        if (!rle_out) {
          fprintf(stderr, "Error: RLE failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        final_out = apply_huffman(rle_out, rle_len, &final_len);
        free_buffer(rle_out);
        if (!final_out) {
          fprintf(stderr, "Error: Huffman failed on chunk %llu\n",
                  (unsigned long long)num_chunks);
          goto chunk_error;
        }
        break;
      }
    }

    /* ---- Encrypt payload if requested ---- */
    if (use_encryption)
      xor_crypt(final_out, final_len, password);

    /* ---- Write chunk header + payload ---- */
    ChunkHeader ch;
    ch.bwt_primary_index     = (uint64_t)primary_index;
    ch.original_chunk_size   = (uint64_t)bytes_read;
    ch.compressed_chunk_size = (uint64_t)final_len;

    fwrite(&ch,        sizeof(ch), 1,         out_file);
    fwrite(final_out,  1,          final_len,  out_file);
    free_buffer(final_out);

    printf("  chunk %llu: %zu -> %zu bytes (%.1f%%)\n",
           (unsigned long long)num_chunks,
           bytes_read, final_len,
           100.0 * (double)final_len / (double)bytes_read);

    num_chunks++;
    continue;

chunk_error:
    free(read_buffer);
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  free(read_buffer);
  fclose(in_file);

  /* Rewrite the real chunk count in the file header */
  fseek(out_file, hdr_offset, SEEK_SET);
  fhdr.num_chunks = num_chunks;
  fwrite(&fhdr, sizeof(fhdr), 1, out_file);
  fclose(out_file);

  printf("Compression complete: %llu chunk(s).\n",
         (unsigned long long)num_chunks);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Decompression                                                       */
/* ------------------------------------------------------------------ */

/*
 * decompress_file_by_chunks — read the file header to determine the
 * level and encryption flag, then reverse the appropriate pipeline for
 * each chunk.  If the archive is encrypted, password must be provided.
 */
int decompress_file_by_chunks(const char* input_filename,
                              const char* output_filename,
                              const char* password) {
  FILE* in_file  = fopen(input_filename,  "rb");
  FILE* out_file = fopen(output_filename, "wb");

  if (!in_file || !out_file) {
    fprintf(stderr, "Error: cannot open '%s' or '%s'\n",
            input_filename, output_filename);
    if (in_file)  fclose(in_file);
    if (out_file) fclose(out_file);
    return -1;
  }

  /* Read and validate file header */
  FileHeader fhdr;
  if (fread(&fhdr, sizeof(fhdr), 1, in_file) != 1 ||
      fhdr.magic != SUDO_MAGIC) {
    fprintf(stderr, "Error: '%s' is not a valid .sudo archive\n",
            input_filename);
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  int level          = (int)fhdr.level;
  int use_encryption = (int)fhdr.encrypted;
  uint64_t num_chunks = fhdr.num_chunks;

  /* Validate level */
  if (level < 1 || level > 5) {
    fprintf(stderr, "Error: archive has unknown compression level %d\n",
            level);
    fclose(in_file);
    fclose(out_file);
    return -1;
  }

  /* Encryption checks */
  if (use_encryption && (!password || password[0] == '\0')) {
    fprintf(stderr,
            "Error: archive is encrypted — provide a password with -p\n");
    fclose(in_file);
    fclose(out_file);
    return -1;
  }
  if (!use_encryption && password && password[0] != '\0') {
    fprintf(stderr,
            "Warning: archive is not encrypted; the provided password is "
            "ignored.\n");
  }

  printf("Decompressing %llu chunk(s) (level %d%s)...\n",
         (unsigned long long)num_chunks, level,
         use_encryption ? ", encrypted" : "");

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

    /* ---- Decrypt if needed (before decompression) ---- */
    if (use_encryption)
      xor_crypt(comp_data, ch.compressed_chunk_size, password);

    /* ---- Reverse the pipeline for the chosen level ---- */
    uint8_t* orig     = NULL;
    size_t   orig_len = 0;

    switch (level) {

      /* Level 1: inverse Huffman */
      case 1: {
        orig = inverse_huffman(comp_data, ch.compressed_chunk_size, &orig_len);
        free_buffer(comp_data);
        if (!orig) {
          fprintf(stderr, "Error: inverse_huffman failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        break;
      }

      /* Level 2: inverse Huffman -> inverse RLE */
      case 2: {
        size_t rle_len = 0;
        uint8_t* rle_data =
            inverse_huffman(comp_data, ch.compressed_chunk_size, &rle_len);
        free_buffer(comp_data);
        if (!rle_data) {
          fprintf(stderr, "Error: inverse_huffman failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        orig = inverse_rle(rle_data, rle_len, &orig_len);
        free_buffer(rle_data);
        if (!orig) {
          fprintf(stderr, "Error: inverse_rle failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        break;
      }

      /* Level 3: inverse Huffman -> inverse RLE -> inverse MTF */
      case 3: {
        size_t rle_len = 0;
        uint8_t* rle_data =
            inverse_huffman(comp_data, ch.compressed_chunk_size, &rle_len);
        free_buffer(comp_data);
        if (!rle_data) {
          fprintf(stderr, "Error: inverse_huffman failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        size_t mtf_len = 0;
        uint8_t* mtf_data = inverse_rle(rle_data, rle_len, &mtf_len);
        free_buffer(rle_data);
        if (!mtf_data) {
          fprintf(stderr, "Error: inverse_rle failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        orig = inverse_mtf(mtf_data, mtf_len);
        orig_len = mtf_len;   /* inverse_mtf preserves length */
        free_buffer(mtf_data);
        if (!orig) {
          fprintf(stderr, "Error: inverse_mtf failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        break;
      }

      /* Level 4 & 5: inv-Huffman -> inv-RLE -> inv-MTF -> inv-BWT */
      default: {
        size_t rle_len = 0;
        uint8_t* rle_data =
            inverse_huffman(comp_data, ch.compressed_chunk_size, &rle_len);
        free_buffer(comp_data);
        if (!rle_data) {
          fprintf(stderr, "Error: inverse_huffman failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        size_t mtf_len = 0;
        uint8_t* mtf_data = inverse_rle(rle_data, rle_len, &mtf_len);
        free_buffer(rle_data);
        if (!mtf_data) {
          fprintf(stderr, "Error: inverse_rle failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        uint8_t* bwt_data = inverse_mtf(mtf_data, mtf_len);
        free_buffer(mtf_data);
        if (!bwt_data) {
          fprintf(stderr, "Error: inverse_mtf failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        orig = inverse_bwt(bwt_data, ch.original_chunk_size,
                           ch.bwt_primary_index);
        orig_len = ch.original_chunk_size;
        free_buffer(bwt_data);
        if (!orig) {
          fprintf(stderr, "Error: inverse_bwt failed at chunk %llu\n",
                  (unsigned long long)c);
          fclose(in_file);
          fclose(out_file);
          return -1;
        }
        break;
      }
    }

    fwrite(orig, 1, orig_len, out_file);
    free_buffer(orig);

    printf("  chunk %llu: decompressed %llu bytes\n",
           (unsigned long long)c,
           (unsigned long long)orig_len);
  }

  fclose(in_file);
  fclose(out_file);
  printf("Decompression complete.\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void print_usage(const char* prog) {
  fprintf(stderr, "================================================\n");
  fprintf(stderr, "SUDO Archiver - Team FAF-252\n");
  fprintf(stderr, "================================================\n");
  fprintf(stderr,
    "Usage (compress): %s -c [-l <1-5>] [-p <password>] <archive.sudo>"
    " <file1> ...\n", prog);
  fprintf(stderr,
    "Usage (extract):  %s -x [-p <password>] <archive.sudo>\n", prog);
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr,
    "  -l <1-5>      Compression level (default: 4)\n"
    "                  1 = Huffman only             (fastest, ~50 KB chunks)\n"
    "                  2 = RLE + Huffman             (~100 KB chunks)\n"
    "                  3 = MTF + RLE + Huffman       (~200 KB chunks)\n"
    "                  4 = BWT + MTF + RLE + Huffman (~400 KB, default)\n"
    "                  5 = BWT + MTF + RLE + Huffman (~800 KB, best ratio)\n");
  fprintf(stderr,
    "  -p <password>  Encrypt/decrypt with XOR cipher (basic obfuscation)\n");
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char* mode_flag = argv[1];   /* -c or -x */
  const char* temp_arc  = "temp_bundle.arc";

  /*
   * Parse all arguments after the mode flag.
   * - "-l <n>" sets the compression level (1-5).
   * - "-p <pass>" sets the password for encryption/decryption.
   * - The first non-flag argument is the archive path.
   * - Any remaining non-flag arguments are input files (compress mode).
   */
  int         level       = 4;     /* default compression level */
  const char* password    = NULL;
  const char* target_sudo = NULL;

  /* At most argc-2 non-flag arguments (mode consumes argv[1]). */
  char** file_buf    = malloc((size_t)(argc + 1) * sizeof(char*));
  char** input_files = file_buf;
  int    num_files   = 0;

  if (!file_buf) {
    fprintf(stderr, "Error: out of memory\n");
    return EXIT_FAILURE;
  }

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
      level = atoi(argv[++i]);
      if (level < 1 || level > 5) {
        fprintf(stderr,
                "Error: invalid level '%s'. Must be between 1 and 5.\n",
                argv[i]);
        free(file_buf);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      password = argv[++i];
    } else if (target_sudo == NULL) {
      target_sudo = argv[i];        /* first positional arg = archive */
    } else {
      input_files[num_files++] = argv[i];  /* remaining = source files */
    }
  }

  if (target_sudo == NULL) {
    fprintf(stderr, "Error: no archive path specified.\n");
    free(file_buf);
    return EXIT_FAILURE;
  }

  /* ==================== COMPRESSION (-c) ==================== */
  if (strcmp(mode_flag, "-c") == 0) {
    if (num_files == 0) {
      fprintf(stderr, "Error: specify at least one file to compress.\n");
      free(file_buf);
      return EXIT_FAILURE;
    }

    printf("1. Bundling files into temporary archive...\n");
    if (arc_create(temp_arc, input_files, num_files) != 0) {
      fprintf(stderr, "Error: failed to bundle files.\n");
      free(file_buf);
      return EXIT_FAILURE;
    }

    printf("2. Compressing bundle (level %d", level);
    switch (level) {
      case 1: printf(": Huffman"); break;
      case 2: printf(": RLE -> Huffman"); break;
      case 3: printf(": MTF -> RLE -> Huffman"); break;
      default: printf(": BWT -> MTF -> RLE -> Huffman"); break;
    }
    if (password && password[0] != '\0')
      printf(" + XOR encryption");
    printf(")...\n");

    if (compress_file_by_chunks(temp_arc, target_sudo, level, password) != 0) {
      fprintf(stderr, "Error: compression pipeline failed.\n");
      remove(temp_arc);
      free(file_buf);
      return EXIT_FAILURE;
    }

    printf("3. Cleaning up...\n");
    remove(temp_arc);

    printf("\n Success: '%s' created.\n", target_sudo);

  /* ==================== EXTRACTION (-x) ==================== */
  } else if (strcmp(mode_flag, "-x") == 0) {

    printf("1. Decompressing '%s'...\n", target_sudo);
    if (decompress_file_by_chunks(target_sudo, temp_arc, password) != 0) {
      fprintf(stderr, "Error: decompression pipeline failed.\n");
      free(file_buf);
      return EXIT_FAILURE;
    }

    printf("2. Extracting files from archive...\n");
    if (arc_extract(temp_arc) != 0) {
      fprintf(stderr, "Error: failed to extract files.\n");
      remove(temp_arc);
      free(file_buf);
      return EXIT_FAILURE;
    }

    printf("3. Cleaning up...\n");
    remove(temp_arc);

    printf("\n Success: files extracted from '%s'.\n", target_sudo);

  /* ==================== UNKNOWN FLAG ==================== */
  } else {
    fprintf(stderr,
            "Error: unknown flag '%s'. Use -c to compress or -x to extract.\n",
            mode_flag);
    free(file_buf);
    return EXIT_FAILURE;
  }

  free(file_buf);
  return EXIT_SUCCESS;
}
