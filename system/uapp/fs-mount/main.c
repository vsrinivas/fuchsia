// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/device/devmgr.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

#include "mount.h"

bool verbose = true;

struct {
    const char* name;
    bool (*detect)(const uint8_t* data);
    int (*mount)(mount_options_t* options);
} FILESYSTEMS[] = {
    // TODO(smklein): Add MemFS support.
    { "minfs", minfs_detect, minfs_mount },
    { "fat", fat_detect, fat_mount },
};

// TODO(smklein): Create "system/ulib/mount". Make both this and devmgr depend
// on it.

int usage(void) {
    fprintf(stderr, "usage: mount [ <option>* ] devicepath mountpath\n");
    fprintf(stderr, " -v            : Verbose mode\n");
    fprintf(stderr, " -r            : Open the filesystem as read-only\n");
    fprintf(stderr, " -f FILESYSTEM : Request a specific filesystem. Otherwise,\n");
    fprintf(stderr, "                 the filesystem type is guessed.\n");
    fprintf(stderr, " Supported filesystems: \n");
    for (size_t i = 0; i < arraylen(FILESYSTEMS); i++) {
        fprintf(stderr, "  %s\n", FILESYSTEMS[i].name);
    }
    return -1;
}

int parse_args(int argc, char** argv, mount_options_t* options) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            verbose = true;
        } else if (!strcmp(argv[1], "-r")) {
            options->readonly = true;
        } else if (!strcmp(argv[1], "-f")) {
            if (argc < 3) {
                return usage();
            }
            for (size_t i = 0; i < arraylen(FILESYSTEMS); i++) {
                if (!strcmp(FILESYSTEMS[i].name, argv[2])) {
                    options->filesystem_index = i;
                    options->filesystem_requested = true;
                    break;
                }
            }
            if (!options->filesystem_requested) {
                fprintf(stderr, "Unsupported filesystem\n");
                return usage();
            }
            argc--;
            argv++;
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc < 3) {
        return usage();
    }
    options->devicepath = argv[1];
    options->mountpath = argv[2];
    return 0;
}

int find_fs_type(mount_options_t* options) {
    if (options->filesystem_requested) {
        xprintf("Requesting filesystem: %s\n",
                FILESYSTEMS[options->filesystem_index].name);
        return 0;
    }

    int fd;
    if ((fd = open(options->devicepath, O_RDONLY)) < 0) {
        fprintf(stderr, "Error opening block device\n");
        return fd;
    }
    uint8_t data[HEADER_SIZE];
    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        close(fd);
        fprintf(stderr, "Error reading block device\n");
        return -1;
    }
    close(fd);

    for (size_t i = 0; i < arraylen(FILESYSTEMS); i++) {
        if (FILESYSTEMS[i].detect(&data[0])) {
            xprintf("Discovered filesystem type: %s\n", FILESYSTEMS[i].name);
            options->filesystem_index = i;
            return 0;
        }
    }
    xprintf("Filesystem does not match any known types\n");
    return -1;
}

int main(int argc, char** argv) {
    mount_options_t options;
    memset(&options, 0, sizeof(mount_options_t));
    int r;
    if ((r = parse_args(argc, argv, &options))) {
        return r;
    }

    xprintf("Mounting device [%s] on path [%s]\n", options.devicepath,
            options.mountpath);

    if ((r = find_fs_type(&options))) {
        return r;
    }

    if ((r = FILESYSTEMS[options.filesystem_index].mount(&options))) {
        return r;
    }
    xprintf("Mounted successfully\n");
    return 0;
}
