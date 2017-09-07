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
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/algorithm.h>

#include "host.h"
#include "misc.h"

#define TRY(func) ({\
    int ret = (func); \
    if (ret < 0) { \
        printf("%s:%d:error: %s -> %d\n", __FILE__, __LINE__, #func, ret); \
        exit(1); \
    } \
    ret; })

#define EXPECT_FAIL(func) ({\
    int ret = (func); \
    if (ret >= 0) { \
        printf("%s:%d:expected error from: %s -> %d\n", __FILE__, __LINE__, #func, ret); \
        exit(1); \
    } \
    ret; })

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
        if ((r = emu_read(w->fd, buffer, xfer)) < 0) {
            fprintf(stderr, "worker('%s') emu_read failed @%u: %s\n",
                    w->name, w->pos, strerror(errno));
            return FAIL;
        }
        if (memcmp(buffer, w->u8 + off, r)) {
            fprintf(stderr, "worker('%s) verify failed @%u\n",
                    w->name, w->pos);
            return FAIL;
        }
    } else {
        if ((r = emu_write(w->fd, w->u8 + off, xfer)) < 0) {
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
        emu_close(w->fd);
    }
    return r;
}

int worker_writer(worker_t* w) {
    int r = worker_rw(w, false);
    if (r == DONE) {
        if (emu_lseek(w->fd, 0, SEEK_SET) != 0) {
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
    if ((w = (worker_t*)calloc(1, sizeof(worker_t))) == nullptr) {
        return -1;
    }

    snprintf(w->name, sizeof(w->name), "%s%s", where, fn);
    srand64(&w->rdata, w->name);
    srand32(&w->rops, w->name);
    w->size = size;
    w->work = work;
    w->flags = flags;

    if ((w->fd = emu_open(w->name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
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

int do_work() {
    uint32_t busy_count = 0;
    for (worker_t* w = all_workers; w != nullptr; w = w->next) {
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

int do_all_work() {
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
    { worker_writer, "file0000", MB(8), F_RAND_IOSIZE, },
    { worker_writer, "file0001", MB(8), F_RAND_IOSIZE, },
    { worker_writer, "file0002", MB(8), F_RAND_IOSIZE, },
    { worker_writer, "file0003", MB(8), F_RAND_IOSIZE, },
    { worker_writer, "file0004", MB(8), 0, },
    { worker_writer, "file0005", MB(8), 0, },
    { worker_writer, "file0006", MB(8), 0, },
    { worker_writer, "file0007", MB(8), 0, },
};

int test_rw1() {
    const char* where = "::";
    for (unsigned n = 0; n < fbl::count_of(WORK); n++) {
        if (worker_new(where, WORK[n].name, WORK[n].work, WORK[n].size, WORK[n].flags) < 0) {
            return -1;
        }
    }
    emu_unlink("::file0007");
    return do_all_work();
}

int test_maxfile() {
    int fd = TRY(emu_open("::bigfile", O_CREAT | O_WRONLY, 0644));
    if (fd < 0) {
        return -1;
    }
    char data[128*1024];
    memset(data, 0xee, sizeof(data));
    ssize_t sz = 0;
    ssize_t r;
    for (;;) {
        if ((r = TRY(emu_write(fd, data, sizeof(data)))) < 0) {
            return -1;
        }
        sz += r;
        if (r < sizeof(data)) {
            break;
        }
    }
    emu_close(fd);
    emu_unlink("::bigfile");
    fprintf(stderr, "wrote %d bytes\n", (int) sz);
    return (r < 0) ? -1 : 0;
}

int test_basic() {
    TRY(emu_mkdir("::alpha", 0755));
    TRY(emu_mkdir("::alpha/bravo", 0755));
    TRY(emu_mkdir("::alpha/bravo/charlie", 0755));
    TRY(emu_mkdir("::alpha/bravo/charlie/delta", 0755));
    TRY(emu_mkdir("::alpha/bravo/charlie/delta/echo", 0755));
    int fd1 = TRY(emu_open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR | O_CREAT, 0644));
    int fd2 = TRY(emu_open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR, 0644));
    TRY(emu_write(fd1, "Hello, World!\n", 14));
    emu_close(fd1);
    emu_close(fd2);
    fd1 = TRY(emu_open("::file.txt", O_CREAT | O_RDWR, 0644));
    emu_close(fd1);
    TRY(emu_unlink("::file.txt"));
    TRY(emu_mkdir("::emptydir", 0755));
    fd1 = TRY(emu_open("::emptydir", O_RDWR, 0644));
    EXPECT_FAIL(emu_unlink("::emptydir"));
    emu_close(fd1);
    TRY(emu_unlink("::emptydir"));
    return 0;
}

int test_rename() {
    EXPECT_FAIL(emu_rename("::alpha", "::bravo")); // Cannot rename when src does not exist
    TRY(emu_mkdir("::alpha", 0755));
    EXPECT_FAIL(emu_rename("::alpha", "::alpha")); // Cannot rename to self
    int fd = TRY(emu_open("::bravo", O_RDWR | O_CREAT | O_EXCL, 0644));
    emu_close(fd);
    EXPECT_FAIL(emu_rename("::alpha", "::bravo")); // Cannot rename dir to file
    TRY(emu_unlink("::bravo"));
    TRY(emu_rename("::alpha", "::bravo")); // Rename dir (dst does not exist)
    TRY(emu_mkdir("::alpha", 0755));
    TRY(emu_rename("::bravo", "::alpha")); // Rename dir (dst does exist)
    fd = TRY(emu_open("::alpha/charlie", O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(emu_rename("::alpha/charlie", "::alpha/delta")); // Rename file (dst does not exist)
    emu_close(fd);
    fd = TRY(emu_open("::alpha/charlie", O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(emu_rename("::alpha/delta", "::alpha/charlie"));     // Rename file (dst does not exist)
    EXPECT_FAIL(emu_rename("::alpha/charlie", "::charlie")); // Cannot rename outside current directory
    emu_close(fd);
    TRY(emu_unlink("::alpha/charlie"));
    TRY(emu_unlink("::alpha"));
    return 0;
}

int run_fs_tests(int argc, char** argv) {
    fprintf(stderr, "--- fs tests ---\n");
    if (argc > 0) {
        if (!strcmp(argv[0], "maxfile")) {
            return test_maxfile();
        }
        if (!strcmp(argv[0], "rw1")) {
            return test_rw1();
        }
        if (!strcmp(argv[0], "basic")) {
            return test_basic();
        }
        if (!strcmp(argv[0], "rename")) {
            return test_rename();
        }
        fprintf(stderr, "unknown test: %s\n", argv[0]);
        return -1;
    }

    return 0;
}
