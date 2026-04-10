/*
 * compresser.c — Multi-threaded SUDO Archiver.
 *
 * Compression pipeline (per chunk): BWT → MTF → RLE → Huffman
 * Decompression pipeline (per chunk): inv-Huffman → inv-RLE → inv-MTF → inv-BWT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "pipeline.h"

#define CHUNK_SIZE (1024 * 1024)
#define MAX_THREADS 8  

typedef struct {
    uint8_t* raw_data;
    size_t size;     
    uint64_t bwt_idx;
    uint8_t* result_data;
    size_t result_len;  
} CompressionTask;

uint64_t global_num_chunks = 0;

void free_buffer(uint8_t* buffer) {
    if (buffer) free(buffer);
}

/* Worker function: Performs the math/transformations only. No File I/O. */
void* compress_worker(void* arg) {
    CompressionTask* t = (CompressionTask*)arg;
    if (!t || !t->raw_data) return NULL;

    /* ---- 1. BWT ---- */
    size_t primary_index = 0;
    uint8_t* bwt_out = apply_bwt(t->raw_data, t->size, &primary_index);
    t->bwt_idx = (uint64_t)primary_index;

    /* ---- 2. MTF ---- */
    uint8_t* mtf_out = apply_mtf(bwt_out, t->size);
    free_buffer(bwt_out);

    /* ---- 3. RLE ---- */
    size_t rle_len = 0;
    uint8_t* rle_out = apply_rle(mtf_out, t->size, &rle_len);
    free_buffer(mtf_out);

    /* ---- 4. Huffman ---- */
    size_t huff_len = 0;
    t->result_data = apply_huffman(rle_out, rle_len, &huff_len);
    t->result_len = huff_len;
    free_buffer(rle_out);

    return NULL;
}

int compress_file_by_chunks(const char* input_filename, const char* output_filename) {
    FILE* in_file = fopen(input_filename, "rb");
    FILE* out_file = fopen(output_filename, "wb");
    if (!in_file || !out_file) {
        if (in_file) fclose(in_file);
        if (out_file) fclose(out_file);
        return -1;
    }

    /* Write SUDO Magic */
    uint32_t magic = SUDO_MAGIC;
    fwrite(&magic, sizeof(magic), 1, out_file);

    /* Write placeholder for total chunk count */
    long count_offset = ftell(out_file);
    uint64_t placeholder = 0;
    fwrite(&placeholder, sizeof(placeholder), 1, out_file);

    pthread_t threads[MAX_THREADS];
    CompressionTask* tasks[MAX_THREADS];

    while (!feof(in_file)) {
        int launched = 0;

        /* Step A: Read data and launch threads for this batch */
        for (int i = 0; i < MAX_THREADS; i++) {
            uint8_t* buffer = malloc(CHUNK_SIZE);
            size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, in_file);

            if (bytes_read == 0) {
                free(buffer);
                break;
            }

            tasks[i] = calloc(1, sizeof(CompressionTask));
            tasks[i]->raw_data = buffer;
            tasks[i]->size = bytes_read;

            if (pthread_create(&threads[i], NULL, compress_worker, tasks[i]) != 0) {
                fprintf(stderr, "Failed to create thread\n");
                break;
            }
            launched++;
        }

        /* Step B: Wait for batch to finish and write results in sequential order */
        for (int i = 0; i < launched; i++) {
            pthread_join(threads[i], NULL);

            struct {
                uint64_t bwt_primary_index;
                uint64_t original_chunk_size;
                uint64_t compressed_chunk_size;
            } __attribute__((packed)) ch;

            ch.bwt_primary_index = tasks[i]->bwt_idx;
            ch.original_chunk_size = (uint64_t)tasks[i]->size;
            ch.compressed_chunk_size = (uint64_t)tasks[i]->result_len;

            /* Sequentially write to output file */
            fwrite(&ch, sizeof(ch), 1, out_file);
            fwrite(tasks[i]->result_data, 1, tasks[i]->result_len, out_file);

            global_num_chunks++;
            printf("\r  Compressing: %llu chunks complete...", (unsigned long long)global_num_chunks);
            fflush(stdout);

            /* Clean up memory for this task */
            free_buffer(tasks[i]->raw_data);
            free_buffer(tasks[i]->result_data);
            free(tasks[i]);
        }

        if (launched < MAX_THREADS) break;
    }

    /* Rewrite the actual final chunk count */
    fseek(out_file, count_offset, SEEK_SET);
    fwrite(&global_num_chunks, sizeof(global_num_chunks), 1, out_file);

    fclose(in_file);
    fclose(out_file);
    printf("\nSuccess. Created %s with %llu chunks.\n", output_filename, (unsigned long long)global_num_chunks);
    return 0;
}

int decompress_file_by_chunks(const char* input_filename, const char* output_filename) {
    FILE* in_file  = fopen(input_filename,  "rb");
    FILE* out_file = fopen(output_filename, "wb");
    if (!in_file || !out_file) {
        if (in_file) fclose(in_file);
        if (out_file) fclose(out_file);
        return -1;
    }

    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, in_file) != 1 || magic != SUDO_MAGIC) {
        fprintf(stderr, "Invalid magic number.\n");
        fclose(in_file); fclose(out_file);
        return -1;
    }

    uint64_t num_chunks = 0;
    fread(&num_chunks, sizeof(num_chunks), 1, in_file);

    for (uint64_t c = 0; c < num_chunks; c++) {
        struct {
            uint64_t bwt_primary_index;
            uint64_t original_chunk_size;
            uint64_t compressed_chunk_size;
        } __attribute__((packed)) ch;

        if (fread(&ch, sizeof(ch), 1, in_file) != 1) break;

        uint8_t* comp_data = malloc(ch.compressed_chunk_size);
        fread(comp_data, 1, ch.compressed_chunk_size, in_file);

        size_t rle_len = 0;
        uint8_t* rle_out = inverse_huffman(comp_data, ch.compressed_chunk_size, &rle_len);
        free_buffer(comp_data);

        size_t mtf_len = 0;
        uint8_t* mtf_out = inverse_rle(rle_out, rle_len, &mtf_len);
        free_buffer(rle_out);

        uint8_t* bwt_out = inverse_mtf(mtf_out, mtf_len);
        free_buffer(mtf_out);

        uint8_t* orig = inverse_bwt(bwt_out, ch.original_chunk_size, ch.bwt_primary_index);
        free_buffer(bwt_out);

        fwrite(orig, 1, ch.original_chunk_size, out_file);
        free_buffer(orig);

        printf("\r  Decompressing chunk %llu/%llu...", (unsigned long long)c + 1, (unsigned long long)num_chunks);
        fflush(stdout);
    }

    fclose(in_file);
    fclose(out_file);
    printf("\nDecompression complete.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main CLI Logic                                                     */
/* ------------------------------------------------------------------ */

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s -c <archive.sudo> <files...> OR %s -x <archive.sudo>\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char* flag = argv[1];
    const char* target_sudo = argv[2];
    const char* temp_arc = "temp_bundle.arc";

    if (strcmp(flag, "-c") == 0) {
        if (argc < 4) return EXIT_FAILURE;
        printf("1. Bundling files...\n");
        if (arc_create(temp_arc, &argv[3], argc - 3) != 0) return EXIT_FAILURE;
        
        printf("2. Compressing (Parallel BWT pipeline)...\n");
        if (compress_file_by_chunks(temp_arc, target_sudo) != 0) return EXIT_FAILURE;
        
        remove(temp_arc);
        printf("Done.\n");
    } else if (strcmp(flag, "-x") == 0) {
        printf("1. Decompressing...\n");
        if (decompress_file_by_chunks(target_sudo, temp_arc) != 0) return EXIT_FAILURE;
        
        printf("2. Extracting files...\n");
        if (arc_extract(temp_arc) != 0) return EXIT_FAILURE;
        
        remove(temp_arc);
        printf("Done.\n");
    } else {
        fprintf(stderr, "Unknown flag: %s\n", flag);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
