// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

#include "filesystems.h"
#include "misc.h"

#define FAIL -1
#define BUSY 0
#define DONE 1

#define FBUFSIZE 65536

static_assert(FBUFSIZE == ((FBUFSIZE / sizeof(uint64_t)) * sizeof(uint64_t)),
              "FBUFSIZE not multiple of uint64_t");

typedef struct worker worker_t;
// global environment variables
typedef struct env {
    worker_t* all_workers;

    list_node_t threads;
} env_t;

typedef struct worker {
    env_t* env;

    worker_t* next;
    int (*work)(worker_t* w);

    rand64_t rdata;
    rand32_t rops;

    int fd;
    int status;
    uint32_t flags;
    uint32_t size;
    uint32_t pos;

    union {
        uint8_t u8[FBUFSIZE];
        uint64_t u64[FBUFSIZE / sizeof(uint64_t)];
    };

    char name[256];
} worker_t;
#define F_RAND_IOSIZE 1


int worker_new(env_t* env, const char* where, const char* fn,
               int (*work)(worker_t* w), uint32_t size, uint32_t flags);
int worker_writer(worker_t* w);
static void init_environment(env_t* env);

typedef struct thread_list {
    list_node_t node;
    thrd_t t;
} thread_list_t;

int worker_rw(worker_t* w, bool do_read) {
    if (w->pos == w->size) {
        return DONE;
    }

    // offset into buffer
    uint32_t off = w->pos % FBUFSIZE;

    // fill our content buffer if it's empty
    if (off == 0) {
        for (unsigned n = 0; n < (FBUFSIZE / sizeof(uint64_t)); n++) {
            w->u64[n] = rand64(&w->rdata);
        }
    }

    // data in buffer available to write
    uint32_t xfer = FBUFSIZE - off;

    // do not exceed our desired size
    if (xfer > (w->size - w->pos)) {
        xfer = w->size - w->pos;
    }

    if ((w->flags & F_RAND_IOSIZE) && (xfer > 3000)) {
        xfer = 3000 + (rand32(&w->rops) % (xfer - 3000));
    }

    int r;
    if (do_read) {
        uint8_t buffer[FBUFSIZE];
        if ((r = read(w->fd, buffer, xfer)) < 0) {
            fprintf(stderr, "worker('%s') read failed @%u: %d\n",
                    w->name, w->pos, errno);
            return FAIL;
        }
        if (memcmp(buffer, w->u8 + off, r)) {
            fprintf(stderr, "worker('%s) verify failed @%u\n",
                    w->name, w->pos);
            return FAIL;
        }
    } else {
        if ((r = write(w->fd, w->u8 + off, xfer)) < 0) {
            fprintf(stderr, "worker('%s') write failed @%u: %d\n",
                    w->name, w->pos, errno);
            return FAIL;
        }
    }

    // advance
    w->pos += r;
    return BUSY;
}

int worker_verify(worker_t* w) {
    int r = worker_rw(w, true);
    if (r == DONE) {
        close(w->fd);
    }
    return r;
}

int worker_writer(worker_t* w) {
    int r = worker_rw(w, false);
    if (r == DONE) {
        if (lseek(w->fd, 0, SEEK_SET) != 0) {
            fprintf(stderr, "worker('%s') seek failed: %s\n",
                    w->name, strerror(errno));
            return FAIL;
        }
        // start at 0 and reset our data generator seed
        srand64(&w->rdata, w->name);
        w->pos = 0;
        w->work = worker_verify;
        return BUSY;
    }
    return r;
}

int worker_new(env_t* env, const char* where, const char* fn,
               int (*work)(worker_t* w), uint32_t size, uint32_t flags) {
    worker_t* w;
    if ((w = calloc(1, sizeof(worker_t))) == NULL) {
        return -1;
    }

    w->env = env;

    snprintf(w->name, sizeof(w->name), "%s%s", where, fn);
    srand64(&w->rdata, w->name);
    srand32(&w->rops, w->name);
    w->size = size;
    w->work = work;
    w->flags = flags;

    if ((w->fd = open(w->name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
        fprintf(stderr, "worker('%s') cannot create file; error %d\n",
                w->name, errno);
        free(w);
        return -1;
    }

    w->next = w->env->all_workers;
    env->all_workers = w;

    return 0;
}

int do_work(env_t* env) {
    uint32_t busy_count = 0;
    for (worker_t* w = env->all_workers; w != NULL; w = w->next) {
        w->env = env;
        if (w->status == BUSY) {
            busy_count++;
            if ((w->status = w->work(w)) == FAIL) {
                TRY(unlink(w->name));
                return FAIL;
            }
            if (w->status == DONE) {
                fprintf(stderr, "worker('%s') finished\n", w->name);
                TRY(unlink(w->name));
            }
        }
    }
    return busy_count ? BUSY : DONE;
}

void do_all_work_single_thread(void) {
    printf("Test Workers (single-threaded)\n");
    env_t env;
    init_environment(&env);

    for (;;) {
        int r = do_work(&env);
        assert(r != FAIL);
        if (r == DONE) {
            break;
        }
    }
}

#define KB(n) ((n)*1024)
#define MB(n) ((n)*1024 * 1024)

struct {
    int (*work)(worker_t*);
    const char* name;
    uint32_t size;
    uint32_t flags;
} WORK[] = {
    {
        worker_writer, "file0000", KB(512), F_RAND_IOSIZE,
    },
    {
        worker_writer, "file0001", MB(10), F_RAND_IOSIZE,
    },
    {
        worker_writer, "file0002", KB(512), F_RAND_IOSIZE,
    },
    {
        worker_writer, "file0003", KB(512), F_RAND_IOSIZE,
    },
    {
        worker_writer, "file0004", KB(512), 0,
    },
    {
        worker_writer, "file0005", MB(20), 0,
    },
    {
        worker_writer, "file0006", KB(512), 0,
    },
    {
        worker_writer, "file0007", KB(512), 0,
    },
};

static void init_environment(env_t* env) {

    // tests are run repeatedly, so reinitialize each time
    env->all_workers = NULL;

    list_initialize(&env->threads);

    // assemble the work
    const char* where = "::";
    for (unsigned n = 0; n < countof(WORK); n++) {
        if (worker_new(env, where, WORK[n].name, WORK[n].work, WORK[n].size, WORK[n].flags) < 0) {
            fprintf(stderr, "failed to create new worker %d\n", n);
            assert(false);
        }
    }
}

static int do_threaded_work(void* arg) {
    worker_t* w = arg;

    fprintf(stderr, "work thread(%s) started\n", w->name);
    while ((w->status = w->work(w)) == BUSY) {
        thrd_yield();
    }

    fprintf(stderr, "work thread(%s) %s\n", w->name,
            w->status == DONE ? "finished" : "failed");
    TRY(unlink(w->name));

    return w->status;
}

static void do_all_work_concurrently(void) {
    printf("Test Workers (multi-threaded)\n");
    env_t env;
    init_environment(&env);

    for (worker_t* w = env.all_workers; w != NULL; w = w->next) {
        // start the workers on separate threads
        thrd_t t;
        int r = thrd_create(&t, do_threaded_work, w);
        if (r == thrd_success) {
            thread_list_t* thread = malloc(sizeof(thread_list_t));
            if (thread == NULL) {
                fprintf(stderr, "couldn't allocate thread list\n");
                assert(false);
            }
            thread->t = t;
            list_add_tail(&env.threads, &thread->node);
        } else {
            fprintf(stderr, "thread create error\n");
            assert(false);
        }
    }

    thread_list_t* next;
    int failed = 0;
    list_for_every_entry(&env.threads, next, thread_list_t, node) {
        int rc;
        int r = thrd_join(next->t, &rc);
        if (r != thrd_success) {
            fprintf(stderr, "thread_join failed: %d\n", r);
            failed++;
        } else if (rc != DONE) {
            fprintf(stderr, "thread exited rc='%s'\n",
                    rc == BUSY ? "busy" : "fail");
            failed++;
        }
    }

    assert(failed == 0);
}

int test_rw_workers(fs_info_t* info) {
    do_all_work_single_thread();
    do_all_work_concurrently();
    return 0;
}
