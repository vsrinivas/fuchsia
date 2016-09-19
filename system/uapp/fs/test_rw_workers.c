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
#include <unistd.h>

#include "misc.h"

#define FAIL -1
#define BUSY 0
#define DONE 1

#define FBUFSIZE 65536

static_assert(FBUFSIZE == ((FBUFSIZE/sizeof(uint64_t)) * sizeof(uint64_t)),
              "FBUFSIZE not multiple of uint64_t");

typedef struct worker worker_t;

struct worker {
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
};

static worker_t* all_workers;

#define F_RAND_IOSIZE 1

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
            fprintf(stderr, "worker('%s') read failed @%u: %s\n",
                    w->name, w->pos, strerror(errno));
            return FAIL;
        }
        if (memcmp(buffer, w->u8 + off, r)) {
            fprintf(stderr, "worker('%s) verify failed @%u\n",
                    w->name, w->pos);
            return FAIL;
        }
    } else {
        if ((r = write(w->fd, w->u8 + off, xfer)) < 0) {
            fprintf(stderr, "worker('%s') write failed @%u: %s\n",
                    w->name, w->pos, strerror(errno));
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


int worker_new(const char* where, const char* fn,
               int (*work)(worker_t* w), uint32_t size, uint32_t flags) {
    worker_t* w;
    if ((w = calloc(1, sizeof(worker_t))) == NULL) {
        return -1;
    }

    snprintf(w->name, sizeof(w->name), "%s%s", where, fn);
    srand64(&w->rdata, w->name);
    srand32(&w->rops, w->name);
    w->size = size;
    w->work = work;
    w->flags = flags;

    if ((w->fd = open(w->name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
        fprintf(stderr, "worker('%s') cannot create file\n", w->name);
        free(w);
        return -1;
    }

    if (all_workers) {
        w->next = all_workers;
    }
    all_workers = w;

    return 0;
}

int do_work(void) {
    uint32_t busy_count = 0;
    for (worker_t* w = all_workers; w != NULL; w = w->next) {
        if (w->status == BUSY) {
            busy_count++;
            if ((w->status = w->work(w)) == FAIL) {
                return FAIL;
            }
            if (w->status == DONE) {
                fprintf(stderr, "worker('%s') finished\n", w->name);
            }
        }
    }
    return busy_count ? BUSY : DONE;
}

int do_all_work(void) {
    for (;;) {
        int r = do_work();
        if (r == FAIL) {
            return -1;
        }
        if (r == DONE) {
            return 0;
        }
    }
}

#define KB(n) ((n) * 1024)
#define MB(n) ((n) * 1024 * 1024)

struct {
    int (*work)(worker_t*);
    const char* name;
    uint32_t size;
    uint32_t flags;
} WORK[] = {
    { worker_writer, "file0000", KB(512), F_RAND_IOSIZE, },
    { worker_writer, "file0001", KB(512), F_RAND_IOSIZE, },
    { worker_writer, "file0002", KB(512), F_RAND_IOSIZE, },
    { worker_writer, "file0003", KB(512), F_RAND_IOSIZE, },
    { worker_writer, "file0004", KB(512), 0, },
    { worker_writer, "file0005", KB(512), 0, },
    { worker_writer, "file0006", KB(512), 0, },
    { worker_writer, "file0007", KB(512), 0, },
};

int test_rw_workers(void) {
    const char* where = "::";
    for (unsigned n = 0; n < (sizeof(WORK) / sizeof(WORK[0])); n++) {
        if (worker_new(where, WORK[n].name, WORK[n].work, WORK[n].size, WORK[n].flags) < 0) {
            return -1;
        }
    }
    unlink("::file0007");
    return do_all_work();
}
