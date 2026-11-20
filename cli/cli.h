/*
 * cli.h - Shared pieces of cskv.exe.
 *
 *   main.c      arguments, opening the store, the read-eval-print loop
 *   commands.c  parsing and running one command line
 *   output.c    every response, in human or JSON form
 */
#ifndef CSKV_CLI_H
#define CSKV_CLI_H

#include <stddef.h>
#include <stdint.h>

#include "cskv.h"

/* Longest accepted input line: a max-size value plus room for the command. */
#define CLI_MAX_LINE (CSKV_MAX_VALUE_LEN + 300u)

typedef struct {
    cskv *db;
    int json;            /* 1: one JSON object per line, no prompt/banners */
} cli_ctx;

/* commands.c: run one line. Returns 1 when the user asked to exit. */
int cli_execute(cli_ctx *ctx, char *line);

/* output.c - every function prints exactly one response and flushes. */
void out_ok(const cli_ctx *ctx);
void out_error(const cli_ctx *ctx, cskv_status st, const char *message);
void out_value(const cli_ctx *ctx, const void *val, size_t vlen);
void out_nil(const cli_ctx *ctx);    /* GET of a missing key */
void out_keys(const cli_ctx *ctx);   /* runs cskv_keys and prints the list */
void out_stats(const cli_ctx *ctx, const cskv_stats *s);
void out_compact(const cli_ctx *ctx, uint64_t before, uint64_t after);
void out_recovery(const cli_ctx *ctx, const cskv_recovery_info *r);
void out_open_error(int json, const char *dir, cskv_status st);
void out_help(const cli_ctx *ctx);

#endif /* CSKV_CLI_H */
