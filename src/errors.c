/* errors.c - Human-readable text for every cskv_status value. */
#include "cskv.h"

const char *cskv_strerror(cskv_status s)
{
    switch (s) {
    case CSKV_OK:              return "ok";
    case CSKV_ERR_NOT_FOUND:   return "key not found";
    case CSKV_ERR_INVALID_ARG: return "invalid argument";
    case CSKV_ERR_NO_MEMORY:   return "out of memory";
    case CSKV_ERR_IO:          return "I/O error";
    case CSKV_ERR_CORRUPT:     return "data is corrupted";
    case CSKV_ERR_LOCKED:      return "store is locked by another process";
    case CSKV_ERR_BAD_FORMAT:  return "unsupported or invalid file format";
    }
    return "unknown error";
}
