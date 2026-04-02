#include <stdio.h>
#include <stdlib.h>
#include "pipeline.h"

// Define a chunk size that Dragoș's BWT qsort can handle without freezing
#define CHUNK_SIZE (1000 * 1024)  // 1 MB chunks

void free_buffer(uint8_t* buffer) {
  if (buffer)
    free(buffer);
}

int compress_file_by_chunks(const char* input_filename,
                            const char* output_filename) {
  FILE* in_file = fopen(input_filename, "rb");
  FILE* out_file = fopen(output_filename, "wb");

  if (!in_file || !out_file)
    return -1;

  // Write a dummy header (Ion's archiving logic will handle the real header
  // later)
  SudoHeader header = {0};
  header.magic = SUDO_MAGIC;
  fwrite(&header, sizeof(SudoHeader), 1, out_file);

  uint8_t* read_buffer = malloc(CHUNK_SIZE);
  size_t bytes_read;

  // Process the file chunk by chunk
  while ((bytes_read = fread(read_buffer, 1, CHUNK_SIZE, in_file)) > 0) {
    size_t bwt_primary_index = 0;
    size_t rle_len = 0;
    size_t lz77_len = 0;
    size_t huffman_len = 0;

    // --- STAGE 1: BWT ---
    uint8_t* bwt_out = apply_bwt(read_buffer, bytes_read, &bwt_primary_index);

    // --- STAGE 2: MTF ---
    uint8_t* mtf_out = apply_mtf(bwt_out, bytes_read);
    free_buffer(bwt_out);  // Free previous stage memory

    // --- STAGE 3: RLE ---
    uint8_t* rle_out = apply_rle(mtf_out, bytes_read, &rle_len);
    free_buffer(mtf_out);

    // --- STAGE 4: LZ77 ---
    uint8_t* lz77_out = apply_lz77(rle_out, rle_len, &lz77_len);
    free_buffer(rle_out);

    // --- STAGE 5: HUFFMAN ---
    // (Assuming Danu implements this to return the final byte array)
    uint8_t* final_out = apply_huffman(lz77_out, lz77_len, &huffman_len);
    free_buffer(lz77_out);

    // --- WRITE CHUNK TO DISK ---
    // 1. Write metadata for this specific chunk
    fwrite(&bwt_primary_index, sizeof(size_t), 1, out_file);
    fwrite(&huffman_len, sizeof(size_t), 1,
           out_file);  // Exact size of compressed bytes

    // 2. Write the compressed payload
    fwrite(final_out, 1, huffman_len, out_file);
    free_buffer(final_out);
  }

  free(read_buffer);
  fclose(in_file);
  fclose(out_file);

  printf("Chunked compression complete.\n");
  return 0;
}

// Placeholder for the inverse pipeline (Decompression)
// int decompress_file_by_chunks(const char* input_filename,
//                               const char* output_filename) {
//   // This will run the inverse pipeline:
//   // read chunk -> inverse_huffman -> inverse_lz77 -> inverse_rle ->
//   inverse_mtf
//   // -> inverse_bwt -> write chunk
//   printf("Decompression pipeline executing...\n");
//   return 0;  // Replace with actual implementation later
// }

int main(int argc, char* argv[]) {
  // 1. Validate argument count
  if (argc < 3) {
    fprintf(stderr, "================================================\n");
    fprintf(stderr, "SUDO Archiver - Team FAF-252\n");
    fprintf(stderr, "================================================\n");
    fprintf(stderr,
            "Usage for compression: ./sudo_archiver -c <archive_name.sudo> "
            "<file1> <file2> ...\n");
    fprintf(stderr,
            "Usage for extraction:  ./sudo_archiver -x <archive_name.sudo>\n");
    return EXIT_FAILURE;
  }

  const char* flag = argv[1];
  const char* target_sudo = argv[2];
  const char* temp_arc = "temp_bundle.arc";  // Temporary uncompressed bundle

  // ==========================================
  // COMPRESSION MODE (-c)
  // ==========================================
  if (strcmp(flag, "-c") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Error: Please specify at least one file to compress.\n");
      return EXIT_FAILURE;
    }

    printf("1. Bundling files into temporary archive...\n");
    int num_files = argc - 3;
    char** input_files = &argv[3];

    if (arc_create(temp_arc, input_files, num_files) != 0) {
      fprintf(stderr, "Error: Failed to bundle files.\n");
      return EXIT_FAILURE;
    }

    printf("2. Pushing bundle through compression pipeline (Chunking)...\n");
    if (compress_file_by_chunks(temp_arc, target_sudo) != 0) {
      fprintf(stderr, "Error: Pipeline compression failed.\n");
      remove(temp_arc);  // Clean up
      return EXIT_FAILURE;
    }

    printf("3. Cleaning up temporary files...\n");
    remove(temp_arc);

    printf("\n Success: Archive '%s' created.\n", target_sudo);
  }

  // ==========================================
  // EXTRACTION MODE (-x)
  // ==========================================
  // else if (strcmp(flag, "-x") == 0) {
  //   printf("1. Pushing %s through decompression pipeline...\n", target_sudo);

  //   if (decompress_file_by_chunks(target_sudo, temp_arc) != 0) {
  //     fprintf(stderr, "Error: Pipeline decompression failed.\n");
  //     return EXIT_FAILURE;
  //   }

  //   printf("2. Extracting files from bundled archive...\n");
  //   if (arc_extract(temp_arc) != 0) {
  //     fprintf(stderr, "Error: Failed to extract files.\n");
  //     remove(temp_arc);
  //     return EXIT_FAILURE;
  //   }

  //   printf("3. Cleaning up temporary files...\n");
  //   remove(temp_arc);

  //   printf("\n✅ Success: Archive '%s' extracted.\n", target_sudo);
  // }

  // ==========================================
  // INVALID FLAG
  // ==========================================
  else {
    fprintf(stderr,
            "Error: Unknown flag '%s'. Use -c to compress or -x to extract.\n",
            flag);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}