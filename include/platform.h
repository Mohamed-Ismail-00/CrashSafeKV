/*
 * platform.h - The only place that knows we run on Windows.
 *
 * Every function returns 0 on success and non-zero on failure
 * (or NULL / -1 where noted) and never prints anything.
 */
#ifndef CSKV_PLATFORM_H
#define CSKV_PLATFORM_H

#include <stdint.h>
#include <stdio.h>

/* Create a directory and all missing parents ("a/b/c"). Existing dirs are OK;
 * a path component that exists as a FILE is an error. */
int plat_mkdir_p(const char *path);

/* 1 if the path exists (file or directory), 0 otherwise. */
int plat_file_exists(const char *path);

/* Delete a file. */
int plat_remove(const char *path);

/* Push stdio buffers to the OS, then force the OS to write them to disk. */
int plat_fsync(FILE *fp);

/* Cut the file to exactly `size` bytes and move the position to the new end. */
int plat_truncate(FILE *fp, int64_t size);

/* Atomically replace dst with src (dst may or may not exist). */
int plat_atomic_replace(const char *src, const char *dst);

/* Size of an open file in bytes, or -1 on error. The file position is preserved. */
int64_t plat_file_size(FILE *fp);

/* Take an exclusive lock file. Returns an opaque handle, or NULL if it is held
 * by another process (or cannot be created). The file disappears on release. */
void *plat_lock_acquire(const char *lock_path);
void  plat_lock_release(void *handle);

/* Monotonic clock in milliseconds (for timing only). */
double plat_now_ms(void);

/* CLI only: read stdin as raw bytes when it is a pipe (GUI, tests). */
void plat_stdin_binary_if_piped(void);

#endif /* CSKV_PLATFORM_H */
