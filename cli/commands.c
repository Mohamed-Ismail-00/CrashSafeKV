/*
 * commands.c - Parse and run one command line.
 *
 * Line format:  COMMAND [args...]
 *   - the command word is case-insensitive
 *   - keys are single tokens: printable ASCII, no spaces (checked here; the
 *     engine itself accepts any bytes)
 *   - SET's value is the rest of the line after the key, spaces included
 *   - SNAPSHOT/RESTORE take the rest of the line as a path
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

/* ---- tokenizing --------------------------------------------------------- */

static char *skip_spaces(char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Returns the next space-separated token ("" if none) and advances *cur. */
static char *next_token(char **cur)
{
    char *start = skip_spaces(*cur);
    char *end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t')
        end++;
    *cur = end;
    if (*end != '\0') {
        *end = '\0';
        *cur = end + 1;
    }
    return start;
}

/* Everything after the current position, without surrounding spaces. */
static char *rest_trimmed(char *cur)
{
    char *start = skip_spaces(cur);
    size_t n = strlen(start);
    while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t'))
        start[--n] = '\0';
    return start;
}

static int same_word(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0' &&
           toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ---- validation --------------------------------------------------------- */

/* Prints an error and returns -1 if `key` is not a valid CLI key. */
static int check_key(const cli_ctx *ctx, const char *key)
{
    size_t n = strlen(key);
    if (n == 0) {
        out_error(ctx, CSKV_ERR_INVALID_ARG, "missing key");
        return -1;
    }
    if (n > CSKV_MAX_KEY_LEN) {
        out_error(ctx, CSKV_ERR_INVALID_ARG, "key too long (max 256 bytes)");
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)key[i];
        if (c < 0x21 || c > 0x7E) {
            out_error(ctx, CSKV_ERR_INVALID_ARG,
                      "key must be printable ASCII without spaces");
            return -1;
        }
    }
    return 0;
}

/* Parses "<key>" with nothing after it. Returns NULL (error printed) if bad. */
static char *single_key(const cli_ctx *ctx, char *args, const char *usage)
{
    char *key = next_token(&args);
    if (check_key(ctx, key) != 0)
        return NULL;
    if (*rest_trimmed(args) != '\0') {
        out_error(ctx, CSKV_ERR_INVALID_ARG, usage);
        return NULL;
    }
    return key;
}

static int no_args(const cli_ctx *ctx, char *args, const char *usage)
{
    if (*rest_trimmed(args) == '\0')
        return 1;
    out_error(ctx, CSKV_ERR_INVALID_ARG, usage);
    return 0;
}

/* ---- commands ----------------------------------------------------------- */

static void cmd_set(cli_ctx *ctx, char *args)
{
    char *key = next_token(&args);
    if (check_key(ctx, key) != 0)
        return;
    char *val = skip_spaces(args);
    size_t vlen = strlen(val);
    if (vlen == 0) {
        out_error(ctx, CSKV_ERR_INVALID_ARG, "usage: SET <key> <value>");
        return;
    }
    if (vlen > CSKV_MAX_VALUE_LEN) {
        out_error(ctx, CSKV_ERR_INVALID_ARG,
                  "value too large (max 1048576 bytes)");
        return;
    }
    cskv_status st = cskv_set(ctx->db, key, strlen(key), val, vlen);
    if (st == CSKV_OK)
        out_ok(ctx);
    else
        out_error(ctx, st, NULL);
}

static void cmd_get(cli_ctx *ctx, char *args)
{
    char *key = single_key(ctx, args, "usage: GET <key>");
    if (key == NULL)
        return;
    void *val = NULL;
    size_t vlen = 0;
    cskv_status st = cskv_get(ctx->db, key, strlen(key), &val, &vlen);
    if (st == CSKV_OK)
        out_value(ctx, val, vlen);
    else if (st == CSKV_ERR_NOT_FOUND)
        out_nil(ctx);
    else
        out_error(ctx, st, NULL);
    free(val);
}

static void cmd_del(cli_ctx *ctx, char *args)
{
    char *key = single_key(ctx, args, "usage: DEL <key>");
    if (key == NULL)
        return;
    cskv_status st = cskv_del(ctx->db, key, strlen(key));
    if (st == CSKV_OK)
        out_ok(ctx);
    else
        out_error(ctx, st, NULL);
}

static void cmd_keys(cli_ctx *ctx, char *args)
{
    if (no_args(ctx, args, "usage: KEYS"))
        out_keys(ctx);
}

static void cmd_stats(cli_ctx *ctx, char *args)
{
    cskv_stats s;
    if (!no_args(ctx, args, "usage: STATS"))
        return;
    cskv_status st = cskv_get_stats(ctx->db, &s);
    if (st == CSKV_OK)
        out_stats(ctx, &s);
    else
        out_error(ctx, st, NULL);
}

static void cmd_compact(cli_ctx *ctx, char *args)
{
    cskv_stats before, after;
    if (!no_args(ctx, args, "usage: COMPACT"))
        return;
    cskv_get_stats(ctx->db, &before);
    cskv_status st = cskv_compact(ctx->db);
    if (st != CSKV_OK) {
        out_error(ctx, st, NULL);
        return;
    }
    cskv_get_stats(ctx->db, &after);
    out_compact(ctx, before.log_bytes, after.log_bytes);
}

/* SNAPSHOT and RESTORE share argument handling. */
static void cmd_file(cli_ctx *ctx, char *args, int restore)
{
    char *path = rest_trimmed(args);
    if (*path == '\0') {
        out_error(ctx, CSKV_ERR_INVALID_ARG,
                  restore ? "usage: RESTORE <path>" : "usage: SNAPSHOT <path>");
        return;
    }
    cskv_status st = restore ? cskv_restore(ctx->db, path)
                             : cskv_snapshot(ctx->db, path);
    if (st == CSKV_OK)
        out_ok(ctx);
    else
        out_error(ctx, st, NULL);
}

static void cmd_snapshot(cli_ctx *ctx, char *args) { cmd_file(ctx, args, 0); }
static void cmd_restore(cli_ctx *ctx, char *args)  { cmd_file(ctx, args, 1); }

static void cmd_help(cli_ctx *ctx, char *args)
{
    if (no_args(ctx, args, "usage: HELP"))
        out_help(ctx);
}

/* ---- dispatch ----------------------------------------------------------- */

typedef struct {
    const char *name;
    void (*run)(cli_ctx *ctx, char *args);
} command;

static const command COMMANDS[] = {
    {"SET", cmd_set},         {"GET", cmd_get},
    {"DEL", cmd_del},         {"KEYS", cmd_keys},
    {"STATS", cmd_stats},     {"COMPACT", cmd_compact},
    {"SNAPSHOT", cmd_snapshot}, {"RESTORE", cmd_restore},
    {"HELP", cmd_help},
};

int cli_execute(cli_ctx *ctx, char *line)
{
    char *args = line;
    char *word = next_token(&args);

    if (*word == '\0') {
        if (ctx->json)       /* JSON mode answers every line */
            out_error(ctx, CSKV_ERR_INVALID_ARG, "empty command");
        return 0;
    }
    if (same_word(word, "EXIT") || same_word(word, "QUIT")) {
        out_ok(ctx);
        return 1;
    }
    for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
        if (same_word(word, COMMANDS[i].name)) {
            COMMANDS[i].run(ctx, args);
            return 0;
        }
    }

    char msg[96];
    snprintf(msg, sizeof msg, "unknown command '%.40s' (try HELP)", word);
    out_error(ctx, CSKV_ERR_INVALID_ARG, msg);
    return 0;
}
