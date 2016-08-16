// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs-private.h"

int do_minfs_check(bcache_t* bc) {
    return minfs_check(bc);
}

int do_minfs_mount(bcache_t* bc) {
#ifdef __Fuchsia__
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    vfs_rpc_server(vn, "fs:/data");
    return 0;
#else
    error("not supported\n");
    return -1;
#endif
}

extern vnode_t* fake_root;
int run_fs_tests(void);

int do_minfs_test(bcache_t* bc) {
#ifdef __Fuchsia__
    error("not supported\n");
    return -1;
#else
    trace_on(TRACE_ALL);
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    fprintf(stderr, "mounted minfs, root vnode %p\n", vn);
    fake_root = vn;
    return run_fs_tests();
#endif
}

struct {
    const char* name;
    int (*func)(bcache_t* bc);
    uint32_t flags;
} CMDS[] = {
    { "create", minfs_mkfs, O_RDWR | O_CREAT },
    { "mkfs", minfs_mkfs, O_RDWR | O_CREAT },
    { "check", do_minfs_check, O_RDONLY },
    { "fsck", do_minfs_check, O_RDONLY },
    { "mount", do_minfs_mount, O_RDWR },
    { "test", do_minfs_test, O_RDWR },
};

int usage(void) {
    fprintf(stderr, "usage: minfs ( create | check | mount | test) <path> [ <size> ]\n");
    return -1;
}

off_t get_size(int fd) {
    off_t off;
    if ((off = lseek(fd, 0, SEEK_END)) < 0) {
        fprintf(stderr, "error: could not find end of file/device\n");
        return 0;
    }
    return off;
}


int do_bitmap_test(void);

int main(int argc, char** argv) {
    off_t size = 0;

    if ((argc == 2) && (!strcmp(argv[1], "bitmap-test"))) {
        return do_bitmap_test();
    }

    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            trace_on(TRACE_ALL);
        } else {
            break;
        }
        argc--;
        argv++;
    }

    if ((argc < 2) || (argc > 4)) {
        return usage();
    }

    const char* cmd = argv[1];
    const char* fn = "/dev/class/block/000";
    if (argc < 3) {
        fprintf(stderr, "minfs: defaulting to '%s'\n", fn);
    } else {
        fn = argv[2];
    }

    if (argc > 3) {
        char* end;
        size = strtoull(argv[3], &end, 10);
        if (end == argv[3]) {
            return usage();
        }
        switch (end[0]) {
        case 'M':
        case 'm':
            size *= (1024*1024);
            end++;
            break;
        case 'G':
        case 'g':
            size *= (1024*1024*1024);
            end++;
            break;
        }
        if (end[0]) {
            return usage();
        }
    }

    //trace_on(TRACE_ALL);

    int fd;
    uint32_t flags = O_RDWR;
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            flags = CMDS[i].flags;
            goto found;
        }
    }
    return usage();

found:
    if ((fd = open(fn, flags, 0644)) < 0) {
        if (flags & O_CREAT) {
            // temporary workaround for Magenta devfs issue
            flags &= (~O_CREAT);
            goto found;
        }
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
    if (size == 0) {
        size = get_size(fd);
    }
    size /= MINFS_BLOCK_SIZE;

    bcache_t* bc;
    if (bcache_create(&bc, fd, size, MINFS_BLOCK_SIZE, 64) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(bc);
        }
    }
    return -1;
}