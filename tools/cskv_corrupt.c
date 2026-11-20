/*
 * cskv_corrupt.c - Damage a log file on purpose (tests and the crash demo).
 *
 *   cskv_corrupt.exe <logfile> truncate <N>    remove the last N bytes
 *   cskv_corrupt.exe <logfile> flip <offset>   XOR one byte with 0xFF
 *   cskv_corrupt.exe <logfile> garbage <N>     append N random bytes
 *
 * Exit codes: 0 done, 1 bad arguments, 2 file error.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

static int usage(void)
{
    fputs("usage: cskv_corrupt.exe <logfile> truncate <N>\n"
          "       cskv_corrupt.exe <logfile> flip <offset>\n"
          "       cskv_corrupt.exe <logfile> garbage <N>\n", stderr);
    return 1;
}

/* Strict non-negative integer parsing: the whole string must be digits. */
static int parse_count(const char *s, long long *out)
{
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < 0)
        return -1;
    *out = v;
    return 0;
}

static int do_truncate(FILE *fp, long long size, long long n)
{
    if (n > size) {
        fprintf(stderr, "cannot remove %lld bytes: file has only %lld\n", n, size);
        return 2;
    }
    if (plat_truncate(fp, size - n) != 0 || plat_fsync(fp) != 0) {
        fputs("truncate failed\n", stderr);
        return 2;
    }
    printf("truncated last %lld bytes (%lld -> %lld)\n", n, size, size - n);
    return 0;
}

static int do_flip(FILE *fp, long long size, long long offset)
{
    if (offset >= size || offset > LONG_MAX) {
        fprintf(stderr, "offset %lld is outside the file (size %lld)\n",
                offset, size);
        return 2;
    }
    int c;
    if (fseek(fp, (long)offset, SEEK_SET) != 0 || (c = fgetc(fp)) == EOF ||
        fseek(fp, (long)offset, SEEK_SET) != 0 || fputc(c ^ 0xFF, fp) == EOF ||
        plat_fsync(fp) != 0) {
        fputs("flip failed\n", stderr);
        return 2;
    }
    printf("flipped byte at offset %lld (0x%02X -> 0x%02X)\n",
           offset, (unsigned)c, (unsigned)(c ^ 0xFF));
    return 0;
}

static int do_garbage(FILE *fp, long long size, long long n)
{
    srand((unsigned)time(NULL));
    if (fseek(fp, 0, SEEK_END) != 0)
        return 2;
    for (long long i = 0; i < n; i++) {
        if (fputc(rand() & 0xFF, fp) == EOF) {
            fputs("write failed\n", stderr);
            return 2;
        }
    }
    if (plat_fsync(fp) != 0)
        return 2;
    printf("appended %lld random bytes (%lld -> %lld)\n", n, size, size + n);
    return 0;
}

int main(int argc, char **argv)
{
    long long arg;
    if (argc != 4 || parse_count(argv[3], &arg) != 0)
        return usage();
    const char *mode = argv[2];
    if (strcmp(mode, "truncate") != 0 && strcmp(mode, "flip") != 0 &&
        strcmp(mode, "garbage") != 0)
        return usage();

    FILE *fp = fopen(argv[1], "r+b");
    if (fp == NULL) {
        fprintf(stderr, "cannot open '%s'\n", argv[1]);
        return 2;
    }
    long long size = plat_file_size(fp);
    int rc = 2;
    if (size >= 0) {
        if (strcmp(mode, "truncate") == 0)
            rc = do_truncate(fp, size, arg);
        else if (strcmp(mode, "flip") == 0)
            rc = do_flip(fp, size, arg);
        else
            rc = do_garbage(fp, size, arg);
    }
    if (fclose(fp) != 0 && rc == 0)
        rc = 2;
    return rc;
}
