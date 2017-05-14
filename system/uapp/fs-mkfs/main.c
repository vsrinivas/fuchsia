// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

struct {
    const char* name;
    disk_format_t df;
} FILESYSTEMS[] = {
    {"blobstore", DISK_FORMAT_BLOBFS},
    {"minfs", DISK_FORMAT_MINFS},
    {"fat", DISK_FORMAT_FAT},
};

int usage(void) {
    fprintf(stderr, "usage: mkfs [ <option>* ] devicepath filesystem\n");
    fprintf(stderr, " -v  : Verbose mode\n");
    fprintf(stderr, " values for 'filesystem' include:\n");
    for (size_t i = 0; i < countof(FILESYSTEMS); i++) {
        fprintf(stderr, "  '%s'\n", FILESYSTEMS[i].name);
    }
    return -1;
}

int parse_args(int argc, char** argv, bool* verbose, disk_format_t* df, char** devicepath) {
    *verbose = false;
    *df = DISK_FORMAT_UNKNOWN;
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            *verbose = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc < 3) {
        return usage();
    }

    *devicepath = argv[1];
    for (size_t i = 0; i < countof(FILESYSTEMS); i++) {
        if (!strcmp(FILESYSTEMS[i].name, argv[2])) {
            *df = FILESYSTEMS[i].df;
            break;
        }
    }
    if (*df == DISK_FORMAT_UNKNOWN) {
        fprintf(stderr, "fs_mkfs: Cannot format a device with filesystem '%s'\n", argv[2]);
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    bool verbose;
    char* devicepath;
    disk_format_t df;
    int r;
    if ((r = parse_args(argc, argv, &verbose, &df, &devicepath))) {
        return r;
    }

    if (verbose) {
        printf("fs_mkfs: Formatting device [%s]\n", devicepath);
    }

    mkfs_options_t options = default_mkfs_options;
    options.verbose = verbose;

    if ((r = mkfs(devicepath, df, launch_stdio_sync, &options)) < 0) {
        fprintf(stderr, "fs_mkfs: Failed to format device: %d\n", r);
    }
    return r;
}
