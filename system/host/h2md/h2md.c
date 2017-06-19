// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//@doc(docs/h2md.md)

//@ # h2md - Header To Markdown
//
// h2md is a simple tool for generating markdown api docs from headers.
//
// It avoids any dependencies and has a very simple line-oriented parser.
// Whitespace at the start and end of lines is ignored.
//
// Lines starting with `//@` are either a directive to h2md or the start of
// a chunk of markdown.
//
// Markdown chunks are continued on every following line starting
// with `//`.  They are ended by a blank line, or a line of source code.
//
// A line of source code after a markdown chunk is expected to be a function
// or method declaration, which will be terminated (on the same line or
// a later line) by a `{` or `;`. It will be presented as a code block.
//
// Lines starting with `//{` begin a code block, and all following lines
// will be code until a line starting with `//}` is observed.
//
// To start a new document, use a doc directive, like
// `//@doc(docs/my-markdown.md)`
//
// From the start of a doc directive until the next doc directive, any
// generated markdown will be sent to the file specified in the directive.
//
//@end

typedef enum {
    IDLE = 0,
    CODEBLOCK,
    ONEFUNCTION,
    MARKDOWN,
} state_t;

typedef struct {
    FILE* fin;
    FILE* fout;
    char* outfn;
    state_t state;
    size_t ws;
    unsigned verbose;
} ctx_t;

void emit(ctx_t* ctx, const char* fmt, ...) {
    if (ctx->fout) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(ctx->fout, fmt, ap);
        va_end(ap);
    }
}

int close_outfile(ctx_t* ctx, bool ok) {
    if (ctx->fout) {
        fclose(ctx->fout);
        ctx->fout = NULL;
        if (ok) {
            size_t len = strlen(ctx->outfn) + 1;
            char fn[len];
            memcpy(fn, ctx->outfn, len);
            *(strrchr(fn, '.')) = 0;
            if (rename(ctx->outfn, fn) != 0) {
                fprintf(stderr, "h2md: could not rename '%s' to '%s'\n", ctx->outfn, fn);
                unlink(ctx->outfn);
                return -1;
            }
            fprintf(stderr, "h2md: generated '%s'\n", fn);
        } else {
            unlink(ctx->outfn);
        }
    }
    return 0;
}

int open_outfile(ctx_t* ctx, const char* fn) {
    if (close_outfile(ctx, true) < 0) {
        return -1;
    }
    size_t len = strlen(fn);
    if ((ctx->outfn = malloc(len + 6)) == NULL) {
        fprintf(stderr, "h2md: out of memory\n");
        return -1;
    }
    snprintf(ctx->outfn, len + 6, "%s.h2md", fn);
    if ((ctx->fout = fopen(ctx->outfn, "w")) == NULL) {
        fprintf(stderr, "h2md: cannot open '%s' for writing\n", ctx->outfn);
        return -1;
    }
    if (ctx->verbose) {
        fprintf(stderr, "h2md: generating '%s'\n", ctx->outfn);
    }
    return 0;
}

void newstate(ctx_t* ctx, state_t state) {
    switch (ctx->state) {
    case CODEBLOCK:
    case ONEFUNCTION:
        emit(ctx, "```\n");
        break;
    default:
        break;
    }
    ctx->state = state;
}

int process_directive(ctx_t* ctx, char* line, size_t len, size_t ws) {
    ctx->ws = ws;
    if (line[0] == '@') {
        if (!strncmp(line + 1, "end", 3)) {
            close_outfile(ctx, true);
            return 0;
        }
        if (!strncmp(line + 1, "doc(", 4)) {
            line += 5;
            char* x = strchr(line, ')');
            if (x == NULL) {
                fprintf(stderr, "h2md: bad doc directive\n");
                return -1;
            }
            *x = 0;
            newstate(ctx, IDLE);
            return open_outfile(ctx, line);
        }
        if (line[1] != ' ') {
            fprintf(stderr, "h2md: unknown directive: %s\n", line);
            return -1;
        }
        line += 2;
        newstate(ctx, MARKDOWN);
        emit(ctx, "\n%s\n", line);
        return 0;
    } else if (line[0] == '{') {
        if (ctx->state == CODEBLOCK) {
            return 0;
        }
        newstate(ctx, CODEBLOCK);
        emit(ctx, "```\n");
        return 0;
    } else if (line[0] == '}') {
        if (ctx->state == CODEBLOCK) {
            emit(ctx, "```\n");
            ctx->state = IDLE;
        }
        return 0;
    } else {
        fprintf(stderr, "h2md: illegal state\n");
        return -1;
    }
}

int process_comment(ctx_t* ctx, char* line, size_t len, size_t ws) {
    switch (ctx->state) {
    case IDLE:
    case CODEBLOCK:
        return 0;
    case MARKDOWN:
        while (isspace(*line)) {
            line++;
        }
        emit(ctx, "%s\n", line);
        return 0;
    case ONEFUNCTION:
        newstate(ctx, IDLE);
        return 0;
    default:
        fprintf(stderr, "h2md: illegal state\n");
        return -1;
    }
}

int process_source(ctx_t* ctx, char* line, size_t len) {
    switch (ctx->state) {
    case IDLE:
        return 0;
    case CODEBLOCK:
        emit(ctx, "%s\n", line);
        return 0;
    case MARKDOWN:
        // After a markdown comment, a blank line exits
        // markdown mode and a non-blank line switches to
        // "one function" mode
        newstate(ctx, ONEFUNCTION);
        emit(ctx, "```\n");
        // fall through
    case ONEFUNCTION: {
        // align whilespace
        size_t ws = ctx->ws;
        while ((ws > 0) && isspace(*line)) {
            line++;
            len--;
            ws--;
        }
        // omit static inline prefix on decls
        if (ctx->state == ONEFUNCTION &&
            !strncmp(line, "static inline ", 14)) {
            line += 14;
            len -= 14;
        }
        char* x;
        // ; or { ends the decl/definition
        if ((x = strchr(line, ';')) || (x = strchr(line, '{'))) {
            *x-- = 0;
            while (x > line) {
                if (!isspace(*x)) {
                    break;
                }
                *x-- = 0;
            }
            emit(ctx, "%s;\n", line);
            newstate(ctx, IDLE);
        } else {
            emit(ctx, "%s\n", line);
        }
        return 0;
    }
    default:
        fprintf(stderr, "h2md: illegal state\n");
        return -1;
    }
}

int process_empty(ctx_t* ctx) {
    switch (ctx->state) {
    case MARKDOWN:
        newstate(ctx, IDLE);
        return 0;
    case CODEBLOCK:
        emit(ctx, "\n");
        return 0;
    default:
        return 0;
    }
}

int process_line(ctx_t* ctx, char* line) {
    size_t len = strlen(line);

    // trim trailing whilespace
    while (len > 0) {
        if (isspace(line[len - 1])) {
            line[len - 1] = 0;
            len--;
        } else {
            break;
        }
    }

    // count leading whitespace
    size_t ws = 0;
    while (*line && isspace(*line)) {
        ws++;
        line++;
        len--;
    }

    if (len == 0) {
        if (ctx->verbose > 1) {
            fprintf(stderr, "ZZZ:\n");
        }
        return process_empty(ctx);
    }
    // check for C++ comment, which may indicate a directive
    if ((line[0] == '/') && (line[1] == '/')) {
        if ((line[2] == '@') || (line[2] == '{') || (line[2] == '}')) {
            if (ctx->verbose > 1) {
                fprintf(stderr, "DIR: %s\n", line);
            }
            return process_directive(ctx, line + 2, len - 2, ws);
        } else {
            if (ctx->verbose > 1) {
                fprintf(stderr, "COM: %s\n", line);
            }
            return process_comment(ctx, line + 2, len - 2, ws);
        }
    } else {
        if (ctx->verbose > 1) {
            fprintf(stderr, "SRC: %s\n", line);
        }
        return process_source(ctx, line - ws, len + ws);
    }
}

int process(const char* fn, unsigned verbose) {
    ctx_t ctx = {
        .fin = NULL,
        .fout = NULL,
        .outfn = NULL,
        .state = IDLE,
        .ws = 0,
        .verbose = verbose,
    };

    if (ctx.verbose) {
        fprintf(stderr, "h2md: processing '%s'\n", fn);
    }
    if ((ctx.fin = fopen(fn, "r")) == NULL) {
        fprintf(stderr, "h2md: cannot open '%s'\n", fn);
        return -1;
    }

    int r = 0;
    for (;;) {
        char line[4096];
        if (fgets(line, sizeof(line), ctx.fin) == NULL) {
            break;
        }
        if ((r = process_line(&ctx, line)) < 0) {
            if (ctx.outfn) {
                unlink(ctx.outfn);
                goto done;
            }
        }
    }

done:
    fclose(ctx.fin);
    close_outfile(&ctx, (r == 0) ? true : false);
    return r;
}


int main(int argc, char** argv) {
    unsigned verbose = false;
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            verbose++;
        } else if (process(argv[1], verbose) < 0) {
            return -1;
        }
        argc--;
        argv++;
    }
    return 0;
}