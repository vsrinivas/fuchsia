// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/atomic.h>
#include <perftest/results.h>
#include <zircon/syscalls.h>
#include <zircon/device/block.h>
#include <zircon/misc/xorshiftrand.h>
#include <lib/sync/completion.h>

static uint64_t number(const char* str) {
    char* end;
    uint64_t n = strtoull(str, &end, 10);

    uint64_t m = 1;
    switch (*end) {
    case 'G':
    case 'g':
        m = 1024*1024*1024;
        break;
    case 'M':
    case 'm':
        m = 1024*1024;
        break;
    case 'K':
    case 'k':
        m = 1024;
        break;
    }
    return m * n;
}

static void bytes_per_second(uint64_t bytes, uint64_t nanos) {
    double s = ((double)nanos) / ((double)1000000000);
    double rate = ((double)bytes) / s;

    const char* unit = "B";
    if (rate > 1024*1024) {
        unit = "MB";
        rate /= 1024*1024;
    } else if (rate > 1024) {
        unit = "KB";
        rate /= 1024;
    }
    fprintf(stderr, "%g %s/s\n", rate, unit);
}

static void ops_per_second(uint64_t count, uint64_t nanos) {
    double s = ((double)nanos) / ((double)1000000000);
    double rate = ((double)count) / s;
    fprintf(stderr, "%g %s/s\n", rate, "ops");
}

typedef struct {
    int fd;
    zx_handle_t vmo;
    zx_handle_t fifo;
    reqid_t reqid;
    vmoid_t vmoid;
    size_t bufsz;
    block_info_t info;
} blkdev_t;

static void blkdev_close(blkdev_t* blk) {
    if (blk->fd >= 0) {
        close(blk->fd);
    }
    zx_handle_close(blk->vmo);
    zx_handle_close(blk->fifo);
    memset(blk, 0, sizeof(blkdev_t));
    blk->fd = -1;
}

static zx_status_t blkdev_open(int fd, const char* dev, size_t bufsz, blkdev_t* blk) {
    memset(blk, 0, sizeof(blkdev_t));
    blk->fd = fd;
    blk->bufsz = bufsz;

    zx_status_t r;
    if (ioctl_block_get_info(fd, &blk->info) != sizeof(block_info_t)) {
        fprintf(stderr, "error: cannot get block device info for '%s'\n", dev);
        goto fail;
    }
    if (ioctl_block_get_fifos(fd, &blk->fifo) != sizeof(zx_handle_t)) {
        fprintf(stderr, "error: cannot get fifo for '%s'\n", dev);
        goto fail;
    }
    if ((r = zx_vmo_create(bufsz, 0, &blk->vmo)) != ZX_OK) {
        fprintf(stderr, "error: out of memory %d\n", r);
        goto fail;
    }

    zx_handle_t dup;
    if ((r = zx_handle_duplicate(blk->vmo, ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
        fprintf(stderr, "error: cannot duplicate handle %d\n", r);
        goto fail;
    }
    if (ioctl_block_attach_vmo(fd, &dup, &blk->vmoid) != sizeof(vmoid_t)) {
        fprintf(stderr, "error: cannot attach vmo for '%s'\n", dev);
        goto fail;
    }

    return ZX_OK;

fail:
    blkdev_close(blk);
    return ZX_ERR_INTERNAL;
}

typedef struct {
    blkdev_t* blk;
    size_t count;
    size_t xfer;
    uint64_t seed;
    int max_pending;
    bool linear;

    fbl::atomic<int> pending;
    sync_completion_t signal;
} bio_random_args_t;

static fbl::atomic<reqid_t> next_reqid(0);

static int bio_random_thread(void* arg) {
    auto* a = reinterpret_cast<bio_random_args_t*>(arg);

    size_t off = 0;
    size_t count = a->count;
    size_t xfer = a->xfer;

    size_t blksize = a->blk->info.block_size;
    size_t blkcount = ((count * xfer) / blksize) - (xfer / blksize);

    rand64_t r64 = RAND63SEED(a->seed);

    zx_handle_t fifo = a->blk->fifo;
    size_t dev_off = 0;

    while (count > 0) {
        while (a->pending.load() == a->max_pending) {
            sync_completion_wait(&a->signal, ZX_TIME_INFINITE);
            sync_completion_reset(&a->signal);
        }

        block_fifo_request_t req = {};
        req.reqid = next_reqid.fetch_add(1);
        req.vmoid = a->blk->vmoid;
        req.opcode = BLOCKIO_READ;
        req.length = static_cast<uint32_t>(xfer);
        req.vmo_offset = off;

        if (a->linear) {
            req.dev_offset = dev_off;
            dev_off += xfer;
        } else {
            req.dev_offset = (rand64(&r64) % blkcount) * blksize;
        }
        off += xfer;
        if ((off + xfer) > a->blk->bufsz) {
            off = 0;
        }

        req.length /= static_cast<uint32_t>(blksize);
        req.dev_offset /= blksize;
        req.vmo_offset /= blksize;

#if 0
        fprintf(stderr, "IO tid=%u vid=%u op=%x len=%zu vof=%zu dof=%zu\n",
                req.reqid, req.vmoid, req.opcode, req.length, req.vmo_offset, req.dev_offset);
#endif
        zx_status_t r = zx_fifo_write(fifo, sizeof(req), &req, 1, NULL);
        if (r == ZX_ERR_SHOULD_WAIT) {
            r = zx_object_wait_one(fifo, ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                                   ZX_TIME_INFINITE, NULL);
            if (r != ZX_OK) {
                fprintf(stderr, "failed waiting for fifo\n");
                zx_handle_close(fifo);
                return -1;
            }
            continue;
        } else if (r < 0) {
            fprintf(stderr, "error: failed writing fifo\n");
            zx_handle_close(fifo);
            return -1;
        }

        a->pending.fetch_add(1);
        count--;
    }
    return 0;
}


static zx_status_t bio_random(bio_random_args_t* a, uint64_t* _total, zx_time_t* _res) {

    thrd_t t;
    int r;

    size_t count = a->count;
    zx_handle_t fifo = a->blk->fifo;

    zx_time_t t0 = zx_clock_get_monotonic();
    thrd_create(&t, bio_random_thread, a);

    while (count > 0) {
        block_fifo_response_t resp;
        zx_status_t r = zx_fifo_read(fifo, sizeof(resp), &resp, 1, NULL);
        if (r == ZX_ERR_SHOULD_WAIT) {
            r = zx_object_wait_one(fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                   ZX_TIME_INFINITE, NULL);
            if (r != ZX_OK) {
                fprintf(stderr, "failed waiting for fifo: %d\n", r);
                goto fail;
            }
            continue;
        } else if (r < 0) {
            fprintf(stderr, "error: failed reading fifo: %d\n", r);
            goto fail;
        }
        if (resp.status != ZX_OK) {
            fprintf(stderr, "error: io txn failed %d (%zu remaining)\n",
                    resp.status, count);
            goto fail;
        }
        count--;
        if (a->pending.fetch_sub(1) == a->max_pending) {
            sync_completion_signal(&a->signal);
        }
    }

    zx_time_t t1;
    t1 = zx_clock_get_monotonic();

    fprintf(stderr, "waiting for thread to exit...\n");
    thrd_join(t, &r);

    *_res = t1 - t0;
    *_total = a->count * a->xfer;
    return ZX_OK;

fail:
    zx_handle_close(a->blk->fifo);
    thrd_join(t, &r);
    return ZX_ERR_IO;
}

void usage(void) {
    fprintf(stderr, "usage: biotime <option>* <device>\n"
                    "\n"
                    "args:  -bs <num>     transfer block size (multiple of 4K)\n"
                    "       -tt <num>     total bytes to transfer\n"
                    "       -mo <num>     maximum outstanding ops (1..128)\n"
                    "       -linear       transfers in linear order\n"
                    "       -random       random transfers across total range\n"
                    "       -output-file <filename>  destination file for "
                    "writing results in JSON format\n"
                    );
}

#define needparam() do { \
    argc--; argv++; \
    if (argc == 0) { \
        fprintf(stderr, "error: option %s needs a parameter\n", argv[-1]); \
        return -1; \
    } } while (0)
#define nextarg() do { argc--; argv++; } while (0)
#define error(x...) do { fprintf(stderr, x); usage(); return -1; } while (0)

int main(int argc, char** argv) {
    blkdev_t blk;

    bio_random_args_t a = {};
    a.blk = &blk;
    a.xfer = 32768;
    a.seed = 7891263897612ULL;
    a.max_pending = 128;
    a.linear = true;
    const char* output_file = nullptr;

    size_t total = 0;

    nextarg();
    while (argc > 0) {
        if (argv[0][0] != '-') {
            break;
        }
        if (!strcmp(argv[0], "-bs")) {
            needparam();
            a.xfer = number(argv[0]);
            if ((a.xfer == 0) || (a.xfer % 4096)) {
                error("error: block size must be multiple of 4K\n");
            }
        } else if (!strcmp(argv[0], "-tt")) {
            needparam();
            total = number(argv[0]);
        } else if (!strcmp(argv[0], "-mo")) {
            needparam();
            size_t n = number(argv[0]);
            if ((n < 1) || (n > 128)) {
                error("error: max pending must be between 1 and 128\n");
            }
            a.max_pending = static_cast<int>(n);
        } else if (!strcmp(argv[0], "-linear")) {
            a.linear = true;
        } else if (!strcmp(argv[0], "-random")) {
            a.linear = false;
        } else if (!strcmp(argv[0], "-output-file")) {
            needparam();
            output_file = argv[0];
        } else if (!strcmp(argv[0], "-h")) {
            usage();
            return 0;
        } else {
            error("error: unknown option: %s\n", argv[0]);
        }
        nextarg();
    }
    if (argc == 0) {
        error("error: no device specified\n");
    }
    if (argc > 1) {
        error("error: unexpected arguments\n");
    }
    const char* device_filename = argv[0];

    int fd;
    if ((fd = open(device_filename, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", device_filename);
        return -1;
    }
    if (blkdev_open(fd, device_filename, 8*1024*1024, &blk) != ZX_OK) {
        return -1;
    }

    size_t devtotal = blk.info.block_count * blk.info.block_size;

    // default to entire device
    if ((total == 0) || (total > devtotal)) {
        total = devtotal;
    }
    a.count = total / a.xfer;

    zx_time_t res = 0;
    total = 0;
    if (bio_random(&a, &total, &res) != ZX_OK) {
        return -1;
    }

    fprintf(stderr, "%zu bytes in %zu ns: ", total, res);
    bytes_per_second(total, res);
    fprintf(stderr, "%zu ops in %zu ns: ", a.count, res);
    ops_per_second(a.count, res);

    if (output_file) {
        perftest::ResultsSet results;
        auto* test_case = results.AddTestCase(
            "fuchsia.zircon", "BlockDeviceThroughput", "bytes/second");
        double time_in_seconds = static_cast<double>(res) / 1e9;
        test_case->AppendValue(static_cast<double>(total) / time_in_seconds);
        if (!results.WriteJSONFile(output_file)) {
            return 1;
        }
    }

    return 0;
}
