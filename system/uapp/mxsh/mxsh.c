// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <mxu/list.h>

#include "mxsh.h"

#define LINE_MAX 1024

void cputc(uint8_t ch) {
    write(1, &ch, 1);
}

void cputs(const char* s, size_t len) {
    write(1, s, len);
}

int cgetc(void) {
    uint8_t ch;
    for (;;) {
        mxio_wait_fd(0, MXIO_EVT_READABLE, NULL);
        int r = read(0, &ch, 1);
        if (r < 0) {
            return r;
        }
        if (r == 1) {
            return ch;
        }
    }
}

void beep(void) {
}

#define CTRL_C 3
#define BACKSPACE 8
#define NL 10
#define CTRL_L 12
#define CR 13
#define ESC 27
#define DELETE 127

#define EXT_UP 'A'
#define EXT_DOWN 'B'
#define EXT_RIGHT 'C'
#define EXT_LEFT 'D'

typedef struct {
    list_node_t node;
    int len;
    char line[LINE_MAX];
} hitem;

list_node_t history = LIST_INITIAL_VALUE(history);

static const char nl[2] = {'\r', '\n'};
static const char bs[3] = {8, ' ', 8};
static const char erase_line[5] = {ESC, '[', '2', 'K', '\r'};

typedef struct {
    int pos;
    int save_pos;
    hitem* item;
    char save[LINE_MAX];
    char line[LINE_MAX + 1];
} editstate;

void history_add(editstate* es) {
    hitem* item;
    if (es->pos && ((item = malloc(sizeof(hitem))) != NULL)) {
        item->len = es->pos;
        memset(item->line, 0, sizeof(item->line));
        memcpy(item->line, es->line, es->pos);
        list_add_tail(&history, &item->node);
    }
}

int history_up(editstate* es) {
    hitem* next;
    if (es->item) {
        next = list_prev_type(&history, &es->item->node, hitem, node);
        if (next != NULL) {
            es->item = next;
            memcpy(es->line, es->item->line, es->item->len);
            es->pos = es->item->len;
            cputs(erase_line, sizeof(erase_line));
            return 1;
        } else {
            beep();
            return 0;
        }
    } else {
        next = list_peek_tail_type(&history, hitem, node);
        if (next != NULL) {
            es->item = next;
            memset(es->save, 0, sizeof(es->save));
            memcpy(es->save, es->line, es->pos);
            es->save_pos = es->pos;
            es->pos = es->item->len;
            memcpy(es->line, es->item->line, es->pos);
            cputs(erase_line, sizeof(erase_line));
            return 1;
        } else {
            return 0;
        }
    }
}

int history_down(editstate* es) {
    if (es->item == NULL) {
        beep();
        return 0;
    }
    hitem* next = list_next_type(&history, &es->item->node, hitem, node);
    if (next != NULL) {
        es->item = next;
        es->pos = es->item->len;
        memcpy(es->line, es->item->line, es->pos);
    } else {
        memcpy(es->line, es->save, es->save_pos);
        es->pos = es->save_pos;
        es->item = NULL;
    }
    cputs(erase_line, sizeof(erase_line));
    return 1;
}

int readline(editstate* es) {
    int a, b, c;
    es->pos = 0;
    es->save_pos = 0;
    es->item = NULL;
again:
    cputc('>');
    cputc(' ');
    if (es->pos) {
        cputs(es->line, es->pos);
    }
    for (;;) {
        if ((c = cgetc()) < 0) {
            es->item = NULL;
            return c;
        }
        if ((c >= ' ') && (c < 127)) {
            if (es->pos < LINE_MAX) {
                es->line[es->pos++] = c;
                cputc(c);
            }
            beep();
            continue;
        }
        switch (c) {
        case CTRL_C:
            es->pos = 0;
            es->item = NULL;
            cputs(nl, sizeof(nl));
            goto again;
        case CTRL_L:
            cputs(erase_line, sizeof(erase_line));
            goto again;
        case BACKSPACE:
        case DELETE:
        backspace:
            if (es->pos > 0) {
                es->pos--;
                cputs(bs, sizeof(bs));
            } else {
                beep();
            }
            es->item = NULL;
            continue;
        case NL:
        case CR:
            es->line[es->pos] = 0;
            cputs(nl, sizeof(nl));
            history_add(es);
            return 0;
        case ESC:
            if ((a = cgetc()) < 0) {
                return a;
            }
            if ((b = cgetc()) < 0) {
                return b;
            }
            if (a != '[') {
                break;
            }
            switch (b) {
            case EXT_UP:
                if (history_up(es)) {
                    goto again;
                }
                break;
            case EXT_DOWN:
                if (history_down(es)) {
                    goto again;
                }
                break;
            case EXT_RIGHT:
                break;
            case EXT_LEFT:
                goto backspace;
            }
        }
        beep();
    }
}

static int split(char* line, char* argv[], int max) {
    int n = 0;
    while (max > 0) {
        while (isspace(*line))
            line++;
        if (*line == 0)
            break;
        argv[n++] = line;
        max--;
        line++;
        while (*line && (!isspace(*line)))
            line++;
        if (*line == 0)
            break;
        *line++ = 0;
    }
    return n;
}

void joinproc(mx_handle_t p) {
    mx_status_t r;
    mx_signals_t satisfied, satisfiable;

    r = _magenta_handle_wait_one(p, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE,
                                 &satisfied, &satisfiable);
    if (r != NO_ERROR) {
        printf("[process(%x): wait failed? %d]\n", p, r);
        return;
    }

    // read the return code
    mx_process_info_t proc_info;
    r = _magenta_process_get_info(p, &proc_info, sizeof(proc_info));
    if (r != NO_ERROR) {
        printf("[process(%x): process_get_info failed? %d]\n", p, r);
    } else {
        printf("[process(%x): status: %d]\n", p, proc_info.return_code);
    }

    _magenta_handle_close(p);
}

void* joiner(void* arg) {
    joinproc((uintptr_t)arg);
    return NULL;
}

void command(int argc, char** argv, bool runbg) {
    char tmp[LINE_MAX + 32];
    int i;

    for (i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(builtins[i].name, argv[0]))
            continue;
        builtins[i].func(argc, argv);
        return;
    }

    snprintf(tmp, sizeof(tmp), "%s%s",
             (argv[0][0] == '/') ? "" : "/boot/bin/", argv[0]);
    argv[0] = tmp;
    mx_handle_t p = mxio_start_process(argv[0], argc, argv);
    if (p < 0) {
        printf("process failed to start (%d)\n", p);
        return;
    }
    if (runbg) {
        // TODO: migrate to a unified waiter thread once we can wait
        //       on process exit
        pthread_t t;
        if (pthread_create(&t, NULL, joiner, (void*)((uintptr_t)p))) {
            _magenta_handle_close(p);
        }
    } else {
        joinproc(p);
    }
}

void console(void) {
    editstate es;
    char* line;
    bool runbg;
    char* argv[32];
    int argc;
    int len;

    while (readline(&es) == 0) {
        line = es.line;
        if (line[0] == '`') {
            _magenta_debug_send_command(line + 1, strlen(line) - 1);
            continue;
        }
        len = strlen(line);

        // trim whitespace
        while ((len > 0) && (line[len - 1] <= ' ')) {
            len--;
            line[len] = 0;
        }

        // handle backgrounding
        if ((len > 0) && (line[len - 1] == '&')) {
            line[len - 1] = 0;
            runbg = true;
        } else {
            runbg = false;
        }

        // tokenize and execute
        argc = split(line, argv, 32);
        if (argc) {
            command(argc, argv, runbg);
        }
    }
}

int main(int argc, char** argv) {
    const char* banner = "\033]2;mxsh\007\nMXCONSOLE...\n";
    cputs(banner, strlen(banner));
    console();
    return 0;
}
