/*
 * platform.c - Windows implementations of the helpers in platform.h.
 * Keeping these here lets every other module stay plain, portable C11.
 */
#include "platform.h"

#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <windows.h>

int plat_mkdir_p(const char *path)
{
    char buf[MAX_PATH];
    size_t len;

    if (path == NULL || (len = strlen(path)) == 0 || len >= sizeof buf)
        return -1;
    memcpy(buf, path, len + 1);

    /* Walk the path and create every prefix that ends at a separator.
     * Index 0 is skipped so absolute paths like "\dir" or "/dir" work. */
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] != '\\' && buf[i] != '/' && buf[i] != '\0')
            continue;
        char saved = buf[i];
        buf[i] = '\0';
        /* Skip drive roots such as "C:" - they always exist. */
        if (!(i == 2 && buf[1] == ':') && !CreateDirectoryA(buf, NULL)) {
            /* Creating failed: fine only if a DIRECTORY is already there
             * (an existing file with that name is an error). */
            DWORD attr = GetFileAttributesA(buf);
            if (attr == INVALID_FILE_ATTRIBUTES ||
                !(attr & FILE_ATTRIBUTE_DIRECTORY))
                return -1;
        }
        buf[i] = saved;
    }
    return 0;
}

void plat_stdin_binary_if_piped(void)
{
    /* Text-mode input treats byte 0x1A (Ctrl+Z) as end of file, which would
     * end a GUI/test session if a value contained it. Pipes switch to binary;
     * an interactive console keeps Ctrl+Z as the usual "end of input". */
    if (!_isatty(_fileno(stdin)))
        _setmode(_fileno(stdin), _O_BINARY);
}

int plat_file_exists(const char *path)
{
    return path != NULL && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int plat_remove(const char *path)
{
    return DeleteFileA(path) ? 0 : -1;
}

int plat_fsync(FILE *fp)
{
    if (fflush(fp) != 0)
        return -1;
    return _commit(_fileno(fp)) == 0 ? 0 : -1;
}

int plat_truncate(FILE *fp, int64_t size)
{
    if (size < 0 || fflush(fp) != 0)
        return -1;
    if (_chsize_s(_fileno(fp), size) != 0)
        return -1;
    /* Leave the stream positioned at the new end of file. */
    return _fseeki64(fp, size, SEEK_SET) == 0 ? 0 : -1;
}

int plat_atomic_replace(const char *src, const char *dst)
{
    /* WRITE_THROUGH: the call returns only after the rename is on disk. */
    return MoveFileExA(src, dst,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0 : -1;
}

int64_t plat_file_size(FILE *fp)
{
    int64_t pos = _ftelli64(fp);
    if (pos < 0 || _fseeki64(fp, 0, SEEK_END) != 0)
        return -1;
    int64_t size = _ftelli64(fp);
    if (_fseeki64(fp, pos, SEEK_SET) != 0)
        return -1;
    return size;
}

void *plat_lock_acquire(const char *lock_path)
{
    /* Share mode allows readers only, so a second writer-open fails with a
     * sharing violation while we hold the handle. DELETE_ON_CLOSE removes the
     * file automatically, even if the process is killed. */
    HANDLE h = CreateFileA(lock_path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                           NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (void *)h;
}

void plat_lock_release(void *handle)
{
    if (handle != NULL)
        CloseHandle((HANDLE)handle);
}

double plat_now_ms(void)
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}
