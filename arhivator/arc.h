#ifndef ARC_H
#define ARC_H

#include <stdint.h>

/* Magic number at the start of every .arc file */
#define ARC_MAGIC "ARC\0"
#define ARC_MAGIC_LEN 4

/* Maximum length of a stored filename (including null terminator) */
#define ARC_FILENAME_MAX 256

/*
 * Global header — sits at offset 0 in the archive.
 *
 * Layout (8 bytes total):
 *   magic[4]     — "ARC\0"
 *   file_count   — number of entries that follow
 */
typedef struct {
    char     magic[ARC_MAGIC_LEN];
    uint32_t file_count;
} __attribute__((packed)) ArcHeader;

/*
 * Per-file entry header — immediately precedes the raw file data.
 *
 * Layout (272 bytes total):
 *   filename[256]    — null-terminated basename
 *   original_size    — size of the file on disk (bytes)
 *   data_size        — bytes of payload that follow this header
 */
typedef struct {
    char     filename[ARC_FILENAME_MAX];
    uint64_t original_size;
    uint64_t data_size;
} __attribute__((packed)) ArcEntry;

#endif /* ARC_H */
