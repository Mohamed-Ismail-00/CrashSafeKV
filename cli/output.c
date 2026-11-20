/*
 * output.c - All responses of cskv.exe.
 *
 * Human mode:  OK / "value" / (nil) / ERR <message>
 * JSON mode:   exactly one JSON object per line, flushed immediately, so a
 *              program reading stdout line by line never waits or blocks.
 */
#include <stdio.h>

#include "cli.h"

static const char *status_name(cskv_status st)
{
    switch (st) {
    case CSKV_OK:              return "OK";
    case CSKV_ERR_NOT_FOUND:   return "NOT_FOUND";
    case CSKV_ERR_INVALID_ARG: return "INVALID_ARG";
    case CSKV_ERR_NO_MEMORY:   return "NO_MEMORY";
    case CSKV_ERR_IO:          return "IO";
    case CSKV_ERR_CORRUPT:     return "CORRUPT";
    case CSKV_ERR_LOCKED:      return "LOCKED";
    case CSKV_ERR_BAD_FORMAT:  return "BAD_FORMAT";
    }
    return "UNKNOWN";
}

/* Print bytes as a JSON string literal: quotes, backslashes and control
 * characters are escaped; other bytes (including UTF-8) pass through. */
static void json_string(const void *data, size_t n)
{
    const unsigned char *p = data;
    putchar('"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 0x20 || c == 0x7F)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}

static void json_cstr(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    json_string(s, n);
}

static void end_line(void)
{
    putchar('\n');
    fflush(stdout);
}

void out_ok(const cli_ctx *ctx)
{
    fputs(ctx->json ? "{\"ok\":true}" : "OK", stdout);
    end_line();
}

void out_error(const cli_ctx *ctx, cskv_status st, const char *message)
{
    if (message == NULL)
        message = cskv_strerror(st);
    if (!ctx->json) {
        printf("ERR %s", message);
    } else {
        printf("{\"ok\":false,\"error\":\"%s\",\"message\":", status_name(st));
        json_cstr(message);
        putchar('}');
    }
    end_line();
}

void out_nil(const cli_ctx *ctx)
{
    if (ctx->json) {
        out_error(ctx, CSKV_ERR_NOT_FOUND, NULL);
        return;
    }
    fputs("(nil)", stdout);
    end_line();
}

void out_value(const cli_ctx *ctx, const void *val, size_t vlen)
{
    if (ctx->json) {
        fputs("{\"ok\":true,\"value\":", stdout);
        json_string(val, vlen);
        putchar('}');
    } else {
        printf("\"%.*s\"", (int)vlen, (const char *)val);
    }
    end_line();
}

typedef struct {
    const cli_ctx *ctx;
    unsigned long count;
} keys_state;

static int print_key(const void *key, size_t klen, void *user)
{
    keys_state *ks = user;
    ks->count++;
    if (ks->ctx->json) {
        if (ks->count > 1)
            putchar(',');
        json_string(key, klen);
    } else {
        printf("%lu) \"%.*s\"\n", ks->count, (int)klen, (const char *)key);
    }
    return 0;
}

void out_keys(const cli_ctx *ctx)
{
    keys_state ks = {ctx, 0};
    if (ctx->json)
        fputs("{\"ok\":true,\"keys\":[", stdout);
    cskv_status st = cskv_keys(ctx->db, print_key, &ks);
    if (ctx->json) {
        /* Keys are printed as they are produced; an error can only happen
         * before the first one (out of memory while sorting). */
        printf("]%s}", st == CSKV_OK ? "" : ",\"warning\":\"incomplete\"");
        end_line();
    } else if (ks.count == 0) {
        fputs("(empty)", stdout);
        end_line();
    } else {
        fflush(stdout);
    }
}

static void json_recovery_fields(const cskv_recovery_info *r)
{
    printf("\"records\":%llu,\"bytes_valid\":%llu,\"bytes_discarded\":%llu,"
           "\"corruption\":%s,\"reason\":",
           (unsigned long long)r->records_replayed,
           (unsigned long long)r->bytes_valid,
           (unsigned long long)r->bytes_discarded,
           r->corruption_found ? "true" : "false");
    json_cstr(r->reason);
    printf(",\"ms\":%.1f", r->duration_ms);
}

void out_stats(const cli_ctx *ctx, const cskv_stats *s)
{
    if (!ctx->json) {
        printf("live_keys:    %llu\nlog_bytes:    %llu\n"
               "log_records:  %llu\ndead_records: %llu\nlast startup: ",
               (unsigned long long)s->live_keys,
               (unsigned long long)s->log_bytes,
               (unsigned long long)s->log_records,
               (unsigned long long)s->dead_records);
        out_recovery(ctx, &s->last_recovery);
        return;
    }
    printf("{\"ok\":true,\"stats\":{\"live_keys\":%llu,\"log_bytes\":%llu,"
           "\"log_records\":%llu,\"dead_records\":%llu,\"recovery\":{",
           (unsigned long long)s->live_keys,
           (unsigned long long)s->log_bytes,
           (unsigned long long)s->log_records,
           (unsigned long long)s->dead_records);
    json_recovery_fields(&s->last_recovery);
    fputs("}}}", stdout);
    end_line();
}

void out_compact(const cli_ctx *ctx, uint64_t before, uint64_t after)
{
    if (ctx->json)
        printf("{\"ok\":true,\"bytes_before\":%llu,\"bytes_after\":%llu}",
               (unsigned long long)before, (unsigned long long)after);
    else
        printf("OK (log %llu -> %llu bytes)",
               (unsigned long long)before, (unsigned long long)after);
    end_line();
}

void out_recovery(const cli_ctx *ctx, const cskv_recovery_info *r)
{
    if (ctx->json) {
        fputs("{\"event\":\"recovery\",", stdout);
        json_recovery_fields(r);
        putchar('}');
    } else {
        printf("Recovered %llu records in %.1f ms.",
               (unsigned long long)r->records_replayed, r->duration_ms);
        if (r->corruption_found)
            printf(" Corruption detected: %s - discarded %llu bytes.",
                   r->reason, (unsigned long long)r->bytes_discarded);
    }
    end_line();
}

void out_open_error(int json, const char *dir, cskv_status st)
{
    if (json) {
        printf("{\"event\":\"error\",\"ok\":false,\"error\":\"%s\",\"message\":",
               status_name(st));
        json_cstr(cskv_strerror(st));
        putchar('}');
        end_line();
    } else {
        fprintf(stderr, "cskv: cannot open store '%s': %s\n",
                dir, cskv_strerror(st));
    }
}

static const char HELP_TEXT[] =
    "SET <key> <value...>  store a value (value may contain spaces)\n"
    "GET <key>             print a value\n"
    "DEL <key>             delete a key\n"
    "KEYS                  list all keys (sorted)\n"
    "STATS                 show statistics\n"
    "COMPACT               rewrite the log with live keys only\n"
    "SNAPSHOT <path>       save a checksummed backup\n"
    "RESTORE <path>        replace all data with a backup\n"
    "HELP                  this text\n"
    "EXIT | QUIT           close the store and exit";

void out_help(const cli_ctx *ctx)
{
    if (ctx->json) {
        fputs("{\"ok\":true,\"help\":", stdout);
        json_cstr(HELP_TEXT);
        putchar('}');
    } else {
        fputs(HELP_TEXT, stdout);
    }
    end_line();
}
