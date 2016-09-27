// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>

#include <magenta/types.h>
#include <magenta/device/block.h>

#include <mxio/io.h>

static int do_test(const char* dev, mx_off_t offset, mx_off_t count, uint8_t pattern) {
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("Cannot open %s!\n", dev);
        return fd;
    }

    void* buf = NULL;
    // constrain to device size
    ssize_t rc;
    uint64_t size;
    rc = ioctl_block_get_size(fd, &size);
    if (rc != sizeof(size)) {
        printf("Error getting size for %s\n", dev);
        goto fail;
    }
    if (count == UINT64_MAX) {
        count = size;
    }
    count = MIN(count, size - offset);

    // write a multiple of block size
    uint64_t blksize;
    rc = ioctl_block_get_blocksize(fd, &blksize);
    if (rc < 0) {
        printf("Error getting block size for %s\n", dev);
        goto fail;
    }
    count -= count % blksize;

    printf("Writing 0x%02x from offset %" PRIu64 " to %" PRIu64 " (%" PRIu64 " bytes)...", pattern, offset, offset + count, count);

    if (offset) {
        rc = lseek(fd, offset, SEEK_SET);
        if (rc < 0) {
            printf("Error %zd seeking to offset %" PRId64 "\n", rc, offset);
            goto fail;
        }
    }

    buf = malloc(count * 2);
    if (!buf) {
        printf("Out of memory!\n");
        goto fail;
    }
    memset(buf, pattern, count);

    // FIXME: do multiple writes if too big

    ssize_t c = count;
    ssize_t actual = write(fd, buf, c);
    if (actual != c) {
        printf("requested to write %zd bytes, only %zd bytes written!\n", c, actual);
        rc = -1;
        goto fail;
    }

    printf("OK\n");

    printf("Reading back...");

    // reset offset
    rc = lseek(fd, offset, SEEK_SET);
    if (rc < 0) {
        printf("Error %zd seeking to offset %" PRId64 "\n", rc, offset);
        goto fail;
    }

    void* buf2 = buf + c;
    actual = read(fd, buf2, c);
    if (actual != c) {
        printf("requested to read %zd bytes, only %zd bytes written!\n", c, actual);
        rc = -1;
        goto fail;
    }

    rc = memcmp(buf, buf2, count);
    if (rc != 0) {
        printf("Fail\n");
    } else {
        printf("OK\n");
    }
fail:
    if (buf) {
        free(buf);
    }
    close(fd);
    return rc;
}

static uint64_t arg_to_u64(const char* arg) {
    int base = 10;
    if ((arg[0] == '0') && ((arg[1] == 'x') || arg[1] == 'X')) {
        base = 16;
        arg = &arg[2]; // skip "0x"
    }
    return strtoull(arg, NULL, base);
}

int main(int argc, const char** argv) {
    if (argc == 1) {
        printf("not enough arguments!\n");
        goto usage;
    }
    const char* dev = argv[1];
    mx_off_t offset = argc >= 3 ? arg_to_u64(argv[2]) : 0;
    mx_off_t count = argc >= 4 ? arg_to_u64(argv[3]) : UINT64_MAX;

    do_test(dev, offset, count, 0x55);
    do_test(dev, offset, count, 0xaa);
    do_test(dev, offset, count, 0xff);
    do_test(dev, offset, count, 0x00);

    return 0;
usage:
    printf("Usage:\n");
    printf("%s <dev> [<offset>] [<count>]\n", argv[0]);
    return 0;
}
