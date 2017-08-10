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


bool worker_new(env_t* env, const char* where, const char* fn,
                int (*work)(worker_t* w), uint32_t size, uint32_t flags);
int worker_writer(worker_t* w);
static bool init_environment(env_t* env);

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

bool worker_new(env_t* env, const char* where, const char* fn,
                int (*work)(worker_t* w), uint32_t size, uint32_t flags) {
    worker_t* w = calloc(1, sizeof(worker_t));
    ASSERT_NE(w, NULL, "");

    w->env = env;

    snprintf(w->name, sizeof(w->name), "%s%s", where, fn);
    srand64(&w->rdata, w->name);
    srand32(&w->rops, w->name);
    w->size = size;
    w->work = work;
    w->flags = flags;

    ASSERT_GT((w->fd = open(w->name, O_RDWR | O_CREAT | O_EXCL, 0644)), 0, "");

    w->next = w->env->all_workers;
    env->all_workers = w;

    return true;
}

int do_work(env_t* env) {
    uint32_t busy_count = 0;
    for (worker_t* w = env->all_workers; w != NULL; w = w->next) {
        w->env = env;
        if (w->status == BUSY) {
            busy_count++;
            if ((w->status = w->work(w)) == FAIL) {
                EXPECT_EQ(unlink(w->name), 0, "");
                return FAIL;
            }
            if (w->status == DONE) {
                fprintf(stderr, "worker('%s') finished\n", w->name);
                EXPECT_EQ(unlink(w->name), 0, "");
            }
        }
    }
    return busy_count ? BUSY : DONE;
}

bool test_work_single_thread(void) {
    BEGIN_TEST;

    env_t env;
    init_environment(&env);

    for (;;) {
        int r = do_work(&env);
        assert(r != FAIL);
        if (r == DONE) {
            break;
        }
    }

    worker_t* w = env.all_workers;
    worker_t* next;
    while (w != NULL) {
        next = w->next;
        free(w);
        w = next;
    }
    END_TEST;
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

static bool init_environment(env_t* env) {

    // tests are run repeatedly, so reinitialize each time
    env->all_workers = NULL;

    list_initialize(&env->threads);

    // assemble the work
    const char* where = "::";
    for (unsigned n = 0; n < countof(WORK); n++) {
        ASSERT_TRUE(worker_new(env, where, WORK[n].name, WORK[n].work,
                               WORK[n].size, WORK[n].flags), "");
    }
    return true;
}

static int do_threaded_work(void* arg) {
    worker_t* w = arg;

    fprintf(stderr, "work thread(%s) started\n", w->name);
    while ((w->status = w->work(w)) == BUSY) {
        thrd_yield();
    }

    fprintf(stderr, "work thread(%s) %s\n", w->name,
            w->status == DONE ? "finished" : "failed");
    EXPECT_EQ(unlink(w->name), 0, "");

    mx_status_t status = w->status;
    free(w);
    return status;
}

static bool test_work_concurrently(void) {
    BEGIN_TEST;

    env_t env;
    ASSERT_TRUE(init_environment(&env), "");

    for (worker_t* w = env.all_workers; w != NULL; w = w->next) {
        // start the workers on separate threads
        thrd_t t;
        ASSERT_EQ(thrd_create(&t, do_threaded_work, w), thrd_success, "");
        thread_list_t* thread = malloc(sizeof(thread_list_t));
        ASSERT_NE(thread, NULL, "");
        thread->t = t;
        list_add_tail(&env.threads, &thread->node);
    }

    thread_list_t* next;
    thread_list_t* tmp;
    list_for_every_entry_safe(&env.threads, next, tmp, thread_list_t, node) {
        int rc;
        ASSERT_EQ(thrd_join(next->t, &rc), thrd_success, "");
        ASSERT_EQ(rc, DONE, "Thread joined, but failed");
        free(next);
    }

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(rw_workers_test,
    RUN_TEST_MEDIUM(test_work_single_thread)
    RUN_TEST_LARGE(test_work_concurrently)
)
