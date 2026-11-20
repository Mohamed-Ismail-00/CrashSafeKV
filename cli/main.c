/*
 * main.c - cskv.exe: argument parsing and the read-eval-print loop.
 *
 *   cskv.exe [--data-dir DIR] [--sync always|none] [--json] [--help]
 *
 * Exit codes: 0 normal, 1 bad arguments, 2 could not open the store.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "platform.h"

typedef struct {
    const char *data_dir;
    cskv_sync_mode sync;
    int json;
} options;

static void print_usage(FILE *to)
{
    fputs("usage: cskv.exe [--data-dir DIR] [--sync always|none] [--json] [--help]\n"
          "  --data-dir DIR   store location (default: ./data)\n"
          "  --sync MODE      always: commit every write to disk (default)\n"
          "                   none:   faster, may lose recent writes on power loss\n"
          "  --json           machine-readable mode (one JSON object per line)\n",
          to);
}

static int bad_arg(const char *arg, int *exit_code)
{
    fprintf(stderr, "cskv: invalid or incomplete argument '%s'\n", arg);
    print_usage(stderr);
    *exit_code = 1;
    return -1;
}

/* Returns 0 to continue, or -1 to stop with *exit_code
 * (0 after --help, 1 on a bad argument). */
static int parse_args(int argc, char **argv, options *opt, int *exit_code)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0) {
            print_usage(stdout);
            *exit_code = 0;
            return -1;
        } else if (strcmp(a, "--json") == 0) {
            opt->json = 1;
        } else if (strcmp(a, "--data-dir") == 0 && i + 1 < argc) {
            opt->data_dir = argv[++i];
        } else if (strcmp(a, "--sync") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "always") == 0)
                opt->sync = CSKV_SYNC_ALWAYS;
            else if (strcmp(m, "none") == 0)
                opt->sync = CSKV_SYNC_NONE;
            else
                return bad_arg(m, exit_code);
        } else {
            return bad_arg(a, exit_code);
        }
    }
    return 0;
}

/* Reads one line into buf (newline removed).
 * Returns 1 = got a line, 0 = end of input, -1 = line too long (the rest of
 * that line has been read and thrown away). */
static int read_line(char *buf, size_t cap)
{
    if (fgets(buf, (int)cap, stdin) == NULL)
        return 0;
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[--n] = '\0';
        if (n > 0 && buf[n - 1] == '\r')
            buf[--n] = '\0';
        return 1;
    }
    if (feof(stdin))
        return 1;                       /* last line without a newline */
    int c;
    while ((c = getchar()) != EOF && c != '\n')
        ;
    return -1;
}

/* Editors and PowerShell often start text with a UTF-8 byte order mark
 * (EF BB BF). It is invisible but would turn "SET" into an unknown command,
 * so it is removed from the very first line only. */
static void strip_bom(char *line)
{
    if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        memmove(line, line + 3, strlen(line + 3) + 1);
}

static void repl(cli_ctx *ctx)
{
    size_t cap = CLI_MAX_LINE + 2;      /* + newline + terminator */
    char *line = malloc(cap);
    int first = 1;
    if (line == NULL) {
        out_error(ctx, CSKV_ERR_NO_MEMORY, NULL);
        return;
    }
    for (;;) {
        if (!ctx->json) {
            fputs("cskv> ", stdout);
            fflush(stdout);
        }
        int got = read_line(line, cap);
        if (got == 0)
            break;
        if (got < 0) {
            out_error(ctx, CSKV_ERR_INVALID_ARG, "line too long");
            first = 0;
            continue;
        }
        if (first) {
            strip_bom(line);
            first = 0;
        }
        if (cli_execute(ctx, line))
            break;
    }
    free(line);
}

int main(int argc, char **argv)
{
    options opt = {"data", CSKV_SYNC_ALWAYS, 0};
    int exit_code = 0;
    if (parse_args(argc, argv, &opt, &exit_code) != 0)
        return exit_code;
    plat_stdin_binary_if_piped();

    cli_ctx ctx = {NULL, opt.json};
    cskv_status st = cskv_open(opt.data_dir, opt.sync, &ctx.db);
    if (st != CSKV_OK) {
        out_open_error(opt.json, opt.data_dir, st);
        return 2;
    }

    cskv_stats s;
    cskv_get_stats(ctx.db, &s);
    out_recovery(&ctx, &s.last_recovery);
    if (!opt.json)
        puts("Type HELP for commands.");

    repl(&ctx);
    cskv_close(ctx.db);
    return 0;
}
