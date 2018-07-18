// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <lib/fdio/util.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

struct {
    const char* name;
    disk_format_t df;
} FILESYSTEMS[] = {
    {"blobfs", DISK_FORMAT_BLOBFS},
    {"minfs", DISK_FORMAT_MINFS},
    {"fat", DISK_FORMAT_FAT},
};

int usage(void) {
    fprintf(stderr, "usage: mkfs [ <option>* ] devicepath filesystem\n");
    fprintf(stderr, " -h|--help                     Print this message\n");
    fprintf(stderr, " -v|--verbose                  Verbose mode\n");
    fprintf(stderr,
            " -s|--fvm_data_slices SLICES   If block device is on top of a FVM,\n"
            "                               the filesystem will have at least SLICES slices "
            "                               allocated for data.\n");
    fprintf(stderr, " values for 'filesystem' include:\n");
    for (size_t i = 0; i < countof(FILESYSTEMS); i++) {
        fprintf(stderr, "  '%s'\n", FILESYSTEMS[i].name);
    }
    return -1;
}

int parse_args(int argc, char** argv, mkfs_options_t* options, disk_format_t* df,
               char** devicepath) {
    static const struct option cmds[] = {
        {"help", no_argument, NULL, 'h'},
        {"verbose", no_argument, NULL, 'v'},
        {"fvm_data_slices", required_argument, NULL, 's'},
        {0, 0, 0, 0},
    };

    int opt_index = -1;
    int c = -1;

    while ((c = getopt_long(argc, argv, "hvs:", cmds, &opt_index)) >= 0) {
        switch (c) {
        case 'v':
            options->verbose = true;
            break;
        case 's':
            options->fvm_data_slices = strtoul(optarg, NULL, 0);
            if (options->fvm_data_slices == 0) {
                fprintf(stderr, "Invalid Args: %s\n", strerror(errno));
                return usage();
            }
            break;
        case 'h':
            return usage();
        default:
            break;
        };
    };

    if (argc - optind < 1) {
        fprintf(stderr, "Invalid Args: Missing devicepath.\n");
        return usage();
    }

    if (argc - optind < 2) {
        fprintf(stderr, "Invalid Args: Missing filesystem.\n");
        return usage();
    }

    for (size_t i = 0; i < countof(FILESYSTEMS); i++) {
        if (!strcmp(FILESYSTEMS[i].name, argv[argc - 1])) {
            *df = FILESYSTEMS[i].df;
            break;
        }
    }

    if (*df == DISK_FORMAT_UNKNOWN) {
        fprintf(stderr, "fs_mkfs: Cannot format a device with filesystem '%s'\n", argv[2]);
        return usage();
    }

    size_t device_arg = argc - 2;
    *devicepath = argv[device_arg];

    return 0;
}

int main(int argc, char** argv) {
    mkfs_options_t options = default_mkfs_options;
    char* devicepath;
    disk_format_t df;
    int r;
    if ((r = parse_args(argc, argv, &options, &df, &devicepath))) {
        return r;
    }

    if (options.verbose) {
        printf("fs_mkfs: Formatting device [%s]\n", devicepath);
    }

    if ((r = mkfs(devicepath, df, launch_stdio_sync, &options)) < 0) {
        fprintf(stderr, "fs_mkfs: Failed to format device: %d\n", r);
    }
    return r;
}
