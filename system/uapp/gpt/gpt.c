// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h> // for mx_cprng_draw
#include <mxio/io.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define DEFAULT_BLOCKDEV "/dev/class/block/000"

static int cgetc(void) {
    uint8_t ch;
    for (;;) {
        mxio_wait_fd(0, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int r = read(0, &ch, 1);
        if (r < 0) return r;
        if (r == 1) return ch;
    }
}

static char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len) {
    size_t i = 0;
    char* ptr = dst;
    while (i < len) {
        char c = src[i++] & 0x7f;
        if (!c) continue;
        *ptr++ = c;
    }
    return dst;
}

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static char* guid_to_cstring(char* dst, const uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
    return dst;
}

static gpt_device_t* init(const char* dev, bool warn, int* out_fd) {
    if (warn) {
        printf("Using %s... <enter> to continue, any other key to cancel\n", dev);

        int c = cgetc();
        if (c != 10) return NULL;
    }

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("error opening %s\n", dev);
        return NULL;
    }

    uint64_t blocksize;
    ssize_t rc = ioctl_block_get_blocksize(fd, &blocksize);
    if (rc < 0) {
        printf("error getting block size\n");
        close(fd);
        return NULL;
    }

    uint64_t blocks;
    rc = ioctl_block_get_size(fd, &blocks);
    if (rc < 0) {
        printf("error getting device size\n");
        close(fd);
        return NULL;
    }
    blocks /= blocksize;

    printf("blocksize=%" PRIu64 " blocks=%" PRIu64 "\n", blocksize, blocks);

    gpt_device_t* gpt;
    rc = gpt_device_init(fd, blocksize, blocks, &gpt);
    if (rc < 0) {
        printf("error initializing test\n");
        close(fd);
        return NULL;
    }

    *out_fd = fd;
    return gpt;
}

static void commit(gpt_device_t* gpt, int fd) {
    printf("commit\n");
    gpt_device_sync(gpt);
    ioctl_block_rr_part(fd);
}

static void dump_partitions(const char* dev) {
    int fd;
    gpt_device_t* gpt = init(dev, false, &fd);
    if (!gpt) return;

    if (!gpt->valid) {
        printf("No valid GPT found\n");
        return;
    }

    printf("Partition table is valid\n");
    gpt_partition_t* p;
    char name[37];
    char guid[37];
    int i;
    for (i = 0; i < PARTITIONS_COUNT; i++) {
        p = gpt->partitions[i];
        if (!p) break;
        memset(name, 0, 37);
        printf("%d: %s 0x%" PRIx64 " 0x%" PRIx64 " (%" PRIx64 " blocks) %s\n", i, utf16_to_cstring(name, (const uint16_t*)p->name, 36), p->first, p->last, p->last - p->first + 1, guid_to_cstring(guid, (const uint8_t*)p->guid));
    }
    printf("Total: %d partitions\n", i);

    gpt_device_release(gpt);
    close(fd);
}

static void add_partition(const char* dev, uint64_t offset, uint64_t blocks, const char* name) {
    int fd;
    gpt_device_t* gpt = init(dev, true, &fd);
    if (!gpt) return;

    if (!gpt->valid) {
        commit(gpt, fd); // generate a default header
    }

    uint8_t type[16];
    uint8_t guid[16];
    memset(type, 0xff, 16);
    mx_cprng_draw(guid, 16);
    int rc = gpt_partition_add(gpt, name, type, guid, offset, blocks, 0);
    if (rc == 0) {
        printf("add partition: name=%s offset=0x%" PRIx64 " blocks=0x%" PRIx64 "\n", name, offset, blocks);
        commit(gpt, fd);
    }

    gpt_device_release(gpt);
    close(fd);
}

static void remove_partition(const char* dev, int n) {
    int fd;
    gpt_device_t* gpt = init(dev, true, &fd);
    if (!gpt) return;

    if (n >= PARTITIONS_COUNT) {
        return;
    }
    gpt_partition_t* p = gpt->partitions[n];
    if (!p) {
        return;
    }
    int rc = gpt_partition_remove(gpt, p->guid);
    if (rc == 0) {
        char name[37];
        printf("remove partition: n=%d name=%s\n", n, utf16_to_cstring(name, (const uint16_t*)p->name, 36));
        commit(gpt, fd);
    }

    gpt_device_release(gpt);
    close(fd);
}

int main(int argc, char** argv) {
    const char* dev = DEFAULT_BLOCKDEV;
    if (argc == 1) goto usage;

    const char* cmd = argv[1];
    if (!strcmp(cmd, "dump")) {
        dump_partitions(argc > 2 ? argv[2] : dev);
    } else if (!strcmp(cmd, "add")) {
        if (argc < 5) goto usage;
        add_partition(argc > 5 ? argv[5] : dev, strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0), argv[4]);
    } else if (!strcmp(cmd, "remove")) {
        if (argc < 3) goto usage;
        remove_partition(argc > 3 ? argv[3] : dev, strtol(argv[2], NULL, 0));
    } else {
        goto usage;
    }

    return 0;
usage:
    printf("usage:\n");
    printf("dump [<dev>]\n");
    printf("add <offset> <blocks> <name> [<dev>]\n");
    printf("remove <n> [<dev>]\n");
    return 0;
}
