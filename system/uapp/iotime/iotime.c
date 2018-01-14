// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs-management/ramdisk.h>
#include <zircon/syscalls.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/block.h>
#include <block-client/client.h>

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

static zx_time_t iotime_posix(int is_read, int fd, size_t total, size_t bufsz) {
    void* buffer = malloc(bufsz);
    if (buffer == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return ZX_TIME_INFINITE;
    }

    zx_time_t t0 = zx_clock_get(ZX_CLOCK_MONOTONIC);
    size_t n = total;
    const char* fn_name = is_read ? "read" : "write";
    while (n > 0) {
        size_t xfer = (n > bufsz) ? bufsz : n;
        ssize_t r = is_read ? read(fd, buffer, xfer) : write(fd, buffer, xfer);
        if (r < 0) {
            fprintf(stderr, "error: %s() error %d\n", fn_name, errno);
            return ZX_TIME_INFINITE;
        }
        if ((size_t)r != xfer) {
            fprintf(stderr, "error: %s() %zu of %zu bytes processed\n", fn_name, r, xfer);
            return ZX_TIME_INFINITE;
        }
        n -= xfer;
    }
    zx_time_t t1 = zx_clock_get(ZX_CLOCK_MONOTONIC);

    return t1 - t0;
}


static int make_ramdisk(size_t blocks) {
    char ramdisk_path[PATH_MAX];
    if (create_ramdisk(512, blocks / 512, ramdisk_path)) {
        return -1;
    }

    return open(ramdisk_path, O_RDWR);
}

static zx_time_t iotime_block(int is_read, int fd, size_t total, size_t bufsz) {
    if ((total % 4096) || (bufsz % 4096)) {
        fprintf(stderr, "error: total and buffer size must be multiples of 4K\n");
        return ZX_TIME_INFINITE;
    }

    return iotime_posix(is_read, fd, total, bufsz);
}

static zx_time_t iotime_fifo(char* dev, int is_read, int fd, size_t total, size_t bufsz) {
    zx_status_t r;
    zx_handle_t vmo;
    if ((r = zx_vmo_create(bufsz, 0, &vmo)) != ZX_OK) {
        fprintf(stderr, "error: out of memory %d\n", r);
        return ZX_TIME_INFINITE;
    }

    block_info_t info;
    if (ioctl_block_get_info(fd, &info) < 0) {
        fprintf(stderr, "error: cannot get info for '%s'\n", dev);
        return ZX_TIME_INFINITE;
    }

    zx_handle_t fifo;
    if (ioctl_block_get_fifos(fd, &fifo) != sizeof(fifo)) {
        fprintf(stderr, "error: cannot get fifo for '%s'\n", dev);
        return ZX_TIME_INFINITE;
    }

    txnid_t txnid;
    if (ioctl_block_alloc_txn(fd, &txnid) != sizeof(txnid)) {
        fprintf(stderr, "error: cannot allocate txn for '%s'\n", dev);
        return ZX_TIME_INFINITE;
    }

    zx_handle_t dup;
    if ((r = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
        fprintf(stderr, "error: cannot duplicate handle %d\n", r);
        return ZX_TIME_INFINITE;
    }

    vmoid_t vmoid;
    if (ioctl_block_attach_vmo(fd, &dup, &vmoid) != sizeof(vmoid)) {
        fprintf(stderr, "error: cannot attach vmo for '%s'\n", dev);
        return ZX_TIME_INFINITE;
    }

    fifo_client_t* client;
    if ((r = block_fifo_create_client(fifo, &client)) != ZX_OK) {
        fprintf(stderr, "error: cannot create block client for '%s' %d\n", dev, r);
        return ZX_TIME_INFINITE;
    }

    zx_time_t t0 = zx_clock_get(ZX_CLOCK_MONOTONIC);
    size_t n = total;
    while (n > 0) {
        size_t xfer = (n > bufsz) ? bufsz : n;
        block_fifo_request_t request = {
            .txnid = txnid,
            .vmoid = vmoid,
            .opcode = is_read ? BLOCKIO_READ : BLOCKIO_WRITE,
            .length = xfer / info.block_size,
            .vmo_offset = 0,
            .dev_offset = (total - n) / info.block_size,
        };
        if ((r = block_fifo_txn(client, &request, 1)) != ZX_OK) {
            fprintf(stderr, "error: block_fifo_txn error %d\n", r);
            return ZX_TIME_INFINITE;
        }
        n -= xfer;
    }
    zx_time_t t1 = zx_clock_get(ZX_CLOCK_MONOTONIC);
    return t1 - t0;
}

static int usage(void) {
    fprintf(stderr,
            "usage: iotime <read|write> <posix|block|fifo> <device|--ramdisk> <bytes> <bufsize>\n\n"
            "        <bytes> and <bufsize> must be a multiple of 4k for block mode\n"
            "        --ramdisk only supported for block mode\n");
    return -1;
}


int main(int argc, char** argv) {
    if (argc != 6) {
        return usage();
    }

    int is_read = !strcmp(argv[1], "read");
    size_t total = number(argv[4]);
    size_t bufsz = number(argv[5]);

    int fd;
    if (!strcmp(argv[3], "--ramdisk")) {
        if (strcmp(argv[2], "block")) {
            fprintf(stderr, "ramdisk only supported for block\n");
            return -1;
        }
        if ((fd = make_ramdisk(total)) < 0) {
            fprintf(stderr, "error: cannot create %zu-byte ramdisk\n", total);
            return -1;
        }
    } else {
        if ((fd = open(argv[3], is_read ? O_RDONLY : O_WRONLY)) < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", argv[3]);
            return -1;
        }
    }

    zx_time_t res;
    if (!strcmp(argv[2], "posix")) {
        res = iotime_posix(is_read, fd, total, bufsz);
    } else if (!strcmp(argv[2], "block")) {
        res = iotime_block(is_read, fd, total, bufsz);
    } else if (!strcmp(argv[2], "fifo")) {
        res = iotime_fifo(argv[3], is_read, fd, total, bufsz);
    } else {
        fprintf(stderr, "error: unknown mode '%s'\n", argv[2]);
        return -1;
    }

    if (res != ZX_TIME_INFINITE) {
        fprintf(stderr, "%s %zu bytes in %zu ns: ", is_read ? "read" : "write", total, res);
        bytes_per_second(total, res);
        return 0;
    } else {
        return -1;
    }
}
