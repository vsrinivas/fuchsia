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

#include <magenta/types.h>
#include <mxio/io.h>
#include <sys/param.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <ddk/protocol/block.h>

static int do_test(const char* dev, mx_off_t offset, mx_off_t count, uint8_t pattern) {
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("Cannot open %s!\n", dev);
        return fd;
    }
    int rc;
    uint64_t size;
    rc = mxio_ioctl(fd, BLOCK_OP_GET_SIZE, NULL, 0, &size, sizeof(size));
    if (rc != sizeof(size)) {
        printf("Error getting size for %s\n", dev);
        goto fail;
    }
    if (count == UINT64_MAX) {
        count = size;
    }
    count = MIN(count, size - offset);

    printf("Writing 0x%02x from offset %llu to %llu (%llu bytes)...", pattern, offset, offset + count, count);

    if (offset) {
        rc = lseek(fd, offset, SEEK_SET);
        if (rc < 0) {
            printf("Error %d seeking to offset %lld\n", rc, offset);
            goto fail;
        }
    }

    void* buf = malloc(count * 2);
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
        printf("Error %d seeking to offset %lld\n", rc, offset);
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
    close(fd);
    return rc;
}

int main(int argc, const char** argv) {
    if (argc == 1) {
        printf("not enough arguments!\n");
        goto usage;
    }
    const char* dev = argv[1];
    mx_off_t offset = argc >= 3 ? strtoull(argv[2], NULL, 10) : 0;
    mx_off_t count = argc >= 4 ? strtoull(argv[3], NULL, 10) : UINT64_MAX;

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
