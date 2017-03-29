// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/device/ramdisk.h>

uint64_t number(const char* str) {
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

void bytes_per_second(uint64_t bytes, uint64_t nanos) {
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

int usage(void);

int iotime_lread(int argc, char** argv) {
    if (argc != 5) {
        return usage();
    }
    size_t total = number(argv[3]);
    size_t bufsz = number(argv[4]);

    void* buffer = malloc(bufsz);
    if (buffer == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    int fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[2]);
        return -1;
    }

    mx_time_t t0 = mx_time_get(MX_CLOCK_MONOTONIC);
    size_t n = total;
    while (n > 0) {
        size_t xfer = (n > bufsz) ? bufsz : n;
        ssize_t r = read(fd, buffer, xfer);
        if (r < 0) {
            fprintf(stderr, "error: read() error %d\n", errno);
            return -1;
        }
        if ((size_t)r != xfer) {
            fprintf(stderr, "error: read() %zu of %zu bytes read\n", r, xfer);
            return -1;
        }
        n -= xfer;
    }
    mx_time_t t1 = mx_time_get(MX_CLOCK_MONOTONIC);

    fprintf(stderr, "read %zu bytes in %zu ns: ", total, t1 - t0);
    bytes_per_second(total, t1 - t0);
    return 0;
}


int make_ramdisk(size_t blocks) {
    int fd = open("/dev/misc/ramctl", O_RDWR);
    if (fd < 0) {
        return -1;
    }

    ramdisk_ioctl_config_t cfg = {
        .blk_size = 512,
        .blk_count = blocks / 512,
        .name = "iotime",
    };

    if (ioctl_ramdisk_config(fd, &cfg) != NO_ERROR) {
        close(fd);
        return -1;
    }

    close(fd);
    for (unsigned n = 0; n < 10; n++) {
        if ((fd = open("/dev/misc/ramctl/iotime/block", O_RDWR)) >= 0) {
            return fd;
        }
        mx_nanosleep(100000);
    }
    return -1;
}

int iotime_bread(int argc, char** argv) {
    if (argc != 5) {
        return usage();
    }
    size_t total = number(argv[3]);
    size_t bufsz = number(argv[4]);

    if ((total % 4096) || (bufsz % 4096)) {
        fprintf(stderr, "error: total and buffer size must be multiples of 4K\n");
        return -1;
    }

    void* buffer = malloc(bufsz);
    if (buffer == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    int fd;
    if (!strcmp(argv[2], "--ramdisk")) {
        if ((fd = make_ramdisk(total)) < 0) {
            fprintf(stderr, "error: cannot create %zu-byte ramdisk\n", total);
            return -1;
        }
    } else {
        if ((fd = open(argv[2], O_RDONLY)) < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", argv[2]);
            return -1;
        }
    }

    mx_time_t t0 = mx_time_get(MX_CLOCK_MONOTONIC);
    size_t n = total;
    while (n > 0) {
        size_t xfer = (n > bufsz) ? bufsz : n;
        ssize_t r = read(fd, buffer, xfer);
        if (r < 0) {
            fprintf(stderr, "error: read() error %d\n", errno);
            return -1;
        }
        if ((size_t)r != xfer) {
            fprintf(stderr, "error: read() %zu of %zu bytes read\n", r, xfer);
            return -1;
        }
        n -= xfer;
    }
    mx_time_t t1 = mx_time_get(MX_CLOCK_MONOTONIC);

    fprintf(stderr, "read %zu bytes in %zu ns: ", total, t1 - t0);
    bytes_per_second(total, t1 - t0);
    return 0;
}

int usage(void) {
    fprintf(stderr,
            "usage: iotime <op>...\n\n"
            "   op: lread <device> <bytes> <bufsize>   posix linear read\n"
            "       bread <device> <bytes> <bufsize>   block linear read\n");
    return -1;
}


int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    if (!strcmp(argv[1], "lread")) {
        return iotime_lread(argc, argv);
    } else if (!strcmp(argv[1], "bread")) {
        return iotime_bread(argc, argv);
    } else {
        return usage();
    }
}