/*
 * arc.c — A from-scratch file archiver.
 *
 * Usage:
 *   arc c archive.arc file1 file2 ...   create an archive
 *   arc x archive.arc                   extract all files
 *   arc l archive.arc                   list contents
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h> /* stat()     */
#include <libgen.h>   /* basename() */

#include "arc.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

/*
 * Copy up to `n` bytes from `src` to `dst` using a fixed-size buffer.
 * Returns the number of bytes actually copied, or -1 on I/O error.
 */
static long copy_bytes(FILE *dst, FILE *src, uint64_t n)
{
    char buf[65536];
    uint64_t remaining = n;
    size_t chunk;

    while (remaining > 0) {
        chunk = (remaining > sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t got = fread(buf, 1, chunk, src);
        if (got == 0) {
            if (ferror(src)) return -1;
            break; /* EOF before expected — caller will notice */
        }
        if (fwrite(buf, 1, got, dst) != got) return -1;
        remaining -= got;
    }
    return (long)(n - remaining);
}

/* ------------------------------------------------------------------ */
/* Create                                                              */
/* ------------------------------------------------------------------ */

/*
 * arc_create — pack the listed files into a new archive.
 *
 * Strategy:
 *   1. Write a placeholder global header (file_count = 0).
 *   2. For each input file, write its entry header then stream its data.
 *   3. Seek back and rewrite the header with the real file_count.
 */
static int arc_create(const char *archive_path, int file_argc, char **file_argv)
{
    FILE *arc = fopen(archive_path, "wb");
    if (!arc) die(archive_path);

    /* --- placeholder global header --- */
    ArcHeader hdr;
    memcpy(hdr.magic, ARC_MAGIC, ARC_MAGIC_LEN);
    hdr.file_count = 0;
    if (fwrite(&hdr, sizeof(hdr), 1, arc) != 1) die("write header");

    uint32_t count = 0;

    for (int i = 0; i < file_argc; i++) {
        const char *path = file_argv[i];

        /* Open the source file */
        FILE *src = fopen(path, "rb");
        if (!src) {
            fprintf(stderr, "warning: cannot open '%s': %s — skipping\n",
                    path, strerror(errno));
            continue;
        }

        /* Determine file size */
        struct stat st;
        if (stat(path, &st) != 0) {
            fprintf(stderr, "warning: cannot stat '%s': %s — skipping\n",
                    path, strerror(errno));
            fclose(src);
            continue;
        }

        /* Build the entry header */
        ArcEntry entry;
        memset(&entry, 0, sizeof(entry));

        /* Store only the basename so extraction doesn't re-create paths */
        char path_copy[4096];
        strncpy(path_copy, path, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        const char *base = basename(path_copy);
        strncpy(entry.filename, base, ARC_FILENAME_MAX - 1);

        entry.original_size = (uint64_t)st.st_size;
        entry.data_size      = (uint64_t)st.st_size; /* 1:1, no compression */

        /* Write entry header */
        if (fwrite(&entry, sizeof(entry), 1, arc) != 1) die("write entry header");

        /* Stream file data */
        long written = copy_bytes(arc, src, entry.data_size);
        fclose(src);

        if (written < 0 || (uint64_t)written != entry.data_size) {
            fprintf(stderr, "warning: '%s': data truncated in archive\n", path);
        }

        printf("  added: %s (%llu bytes)\n", entry.filename,
               (unsigned long long)entry.original_size);
        count++;
    }

    /* Rewrite the global header with the real count */
    rewind(arc);
    hdr.file_count = count;
    if (fwrite(&hdr, sizeof(hdr), 1, arc) != 1) die("rewrite header");

    fclose(arc);
    printf("Created '%s' with %u file(s).\n", archive_path, count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* List                                                                */
/* ------------------------------------------------------------------ */

static int arc_list(const char *archive_path)
{
    FILE *arc = fopen(archive_path, "rb");
    if (!arc) die(archive_path);

    ArcHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, arc) != 1) die("read header");

    if (memcmp(hdr.magic, ARC_MAGIC, ARC_MAGIC_LEN) != 0) {
        fprintf(stderr, "error: '%s' is not a valid .arc archive\n", archive_path);
        fclose(arc);
        return 1;
    }

    printf("Archive: %s\n", archive_path);
    printf("%-40s  %12s\n", "Filename", "Size");
    printf("%-40s  %12s\n", "--------", "----");

    for (uint32_t i = 0; i < hdr.file_count; i++) {
        ArcEntry entry;
        if (fread(&entry, sizeof(entry), 1, arc) != 1) {
            fprintf(stderr, "error: truncated archive at entry %u\n", i);
            fclose(arc);
            return 1;
        }
        printf("%-40s  %12llu\n",
               entry.filename,
               (unsigned long long)entry.original_size);

        /* Skip past the data to reach the next header */
        if (fseeko(arc, (off_t)entry.data_size, SEEK_CUR) != 0) {
            fprintf(stderr, "error: seek failed at entry %u\n", i);
            fclose(arc);
            return 1;
        }
    }

    fclose(arc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Extract                                                             */
/* ------------------------------------------------------------------ */

static int arc_extract(const char *archive_path)
{
    FILE *arc = fopen(archive_path, "rb");
    if (!arc) die(archive_path);

    ArcHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, arc) != 1) die("read header");

    if (memcmp(hdr.magic, ARC_MAGIC, ARC_MAGIC_LEN) != 0) {
        fprintf(stderr, "error: '%s' is not a valid .arc archive\n", archive_path);
        fclose(arc);
        return 1;
    }

    printf("Extracting from '%s' (%u file(s))...\n", archive_path, hdr.file_count);

    for (uint32_t i = 0; i < hdr.file_count; i++) {
        ArcEntry entry;
        if (fread(&entry, sizeof(entry), 1, arc) != 1) {
            fprintf(stderr, "error: truncated archive at entry %u\n", i);
            fclose(arc);
            return 1;
        }

        /* Safety: ensure filename is null-terminated and not a path traversal */
        entry.filename[ARC_FILENAME_MAX - 1] = '\0';
        if (strchr(entry.filename, '/') || strchr(entry.filename, '\\')) {
            fprintf(stderr, "warning: suspicious filename '%s' — skipping\n",
                    entry.filename);
            if (fseeko(arc, (off_t)entry.data_size, SEEK_CUR) != 0) die("seek");
            continue;
        }

        FILE *out = fopen(entry.filename, "wb");
        if (!out) {
            fprintf(stderr, "warning: cannot create '%s': %s — skipping\n",
                    entry.filename, strerror(errno));
            if (fseeko(arc, (off_t)entry.data_size, SEEK_CUR) != 0) die("seek");
            continue;
        }

        long written = copy_bytes(out, arc, entry.data_size);
        fclose(out);

        if (written < 0 || (uint64_t)written != entry.data_size) {
            fprintf(stderr, "warning: '%s': incomplete extraction\n", entry.filename);
        } else {
            printf("  extracted: %s (%llu bytes)\n",
                   entry.filename,
                   (unsigned long long)entry.original_size);
        }
    }

    fclose(arc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s c <archive.arc> <file1> [file2 ...]   create archive\n"
        "  %s x <archive.arc>                        extract archive\n"
        "  %s l <archive.arc>                        list contents\n",
        prog, prog, prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 3) usage(argv[0]);

    const char *cmd          = argv[1];
    const char *archive_path = argv[2];

    if (strcmp(cmd, "c") == 0) {
        if (argc < 4) usage(argv[0]);
        return arc_create(archive_path, argc - 3, argv + 3);
    } else if (strcmp(cmd, "x") == 0) {
        return arc_extract(archive_path);
    } else if (strcmp(cmd, "l") == 0) {
        return arc_list(archive_path);
    } else {
        usage(argv[0]);
    }
    return 0;
}
