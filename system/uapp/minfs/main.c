// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "minfs-private.h"

int do_minfs_check(bcache_t* bc, int argc, char** argv) {
    return minfs_check(bc);
}

#ifdef __Fuchsia__
int do_minfs_mount(bcache_t* bc, int argc, char** argv) {
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    vfs_rpc_server(vn, "/data");
    return 0;
}
#else
int run_fs_tests(int argc, char** argv);

static bcache_t* the_block_cache;
void drop_cache(void) {
    bcache_invalidate(the_block_cache);
}

extern vnode_t* fake_root;

int io_setup(bcache_t* bc) {
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    fake_root = vn;
    the_block_cache = bc;
    return 0;
}

int do_minfs_test(bcache_t* bc, int argc, char** argv) {
    if (io_setup(bc)) {
        return -1;
    }
    return run_fs_tests(argc, argv);
}

int do_cp(bcache_t* bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "cp requires two arguments\n");
        return -1;
    }

    if (io_setup(bc)) {
        return -1;
    }

    int fdi, fdo;
    if ((fdi = open(argv[0], O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[0]);
        return -1;
    }
    if ((fdo = open(argv[1], O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }

    char buffer[256*1024];
    ssize_t r;
    for (;;) {
        if ((r = read(fdi, buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "error: reading from '%s'\n", argv[0]);
            break;
        } else if (r == 0) {
            break;
        }
        void* ptr = buffer;
        ssize_t len = r;
        while (len > 0) {
            if ((r = write(fdo, ptr, len)) < 0) {
                fprintf(stderr, "error: writing to '%s'\n", argv[1]);
                goto done;
            }
            ptr += r;
            len -= r;
        }
    }
done:
    close(fdi);
    close(fdo);
    return r;
}

#endif

int do_minfs_mkfs(bcache_t* bc, int argc, char** argv) {
    return minfs_mkfs(bc);
}

struct {
    const char* name;
    int (*func)(bcache_t* bc, int argc, char** argv);
    uint32_t flags;
    const char *help;
} CMDS[] = {
    { "create", do_minfs_mkfs,  O_RDWR | O_CREAT, "initialize filesystem" },
    { "mkfs",   do_minfs_mkfs,  O_RDWR | O_CREAT, "initialize filesystem" },
    { "check",  do_minfs_check, O_RDONLY,         "check filesystem integrity"},
    { "fsck",   do_minfs_check, O_RDONLY,         "check filesystem integrity"},
#ifdef __Fuchsia__
    { "mount",  do_minfs_mount, O_RDWR,           "mount filesystem at /data" },
#else
    { "test",   do_minfs_test,  O_RDWR,           "run tests against filesystem" },
    { "cp",     do_cp,          O_RDWR,           "copy to/from fs" },
#endif
};

int usage(void) {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -v         some debug messages\n"
            "          -vv        all debug messages\n"
            "\n");
    for (unsigned n = 0; n < (sizeof(CMDS) / sizeof(CMDS[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

off_t get_size(int fd) {
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "error: could not find end of file/device\n");
        return 0;
    }
    return s.st_size;;
}


int do_bitmap_test(void);

int main(int argc, char** argv) {
    off_t size = 0;

    // handle options
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            trace_on(TRACE_SOME);
        } else if (!strcmp(argv[1], "-vv")) {
            trace_on(TRACE_ALL);
        } else {
            break;
        }
        argc--;
        argv++;
    }

    if (argc < 3) {
        return usage();
    }

    char* fn = argv[1];
    char* cmd = argv[2];

    char* sizestr;
    if ((sizestr = strchr(fn, '@')) != NULL) {
        *sizestr++ = 0;
        char* end;
        size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
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
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
            return usage();
        }
    }

    int fd;
    uint32_t flags = O_RDWR;
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            flags = CMDS[i].flags;
            goto found;
        }
    }
    fprintf(stderr, "minfs: unknown command: %s\n", cmd);
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
            return CMDS[i].func(bc, argc - 3, argv + 3);
        }
    }
    return -1;
}