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
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>


int usage(void) {
    fprintf(stderr, "usage: mount [ <option>* ] devicepath mountpath\n");
    fprintf(stderr, " -v  : Verbose mode\n");
    fprintf(stderr, " -r  : Open the filesystem as read-only\n");
    return -1;
}

int parse_args(int argc, char** argv, mount_options_t* options,
               char** devicepath, char** mountpath) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            options->verbose_mount = true;
        } else if (!strcmp(argv[1], "-r")) {
            options->readonly = true;
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
    *mountpath = argv[2];
    return 0;
}

int main(int argc, char** argv) {
    char* devicepath;
    char* mountpath;
    mount_options_t options = default_mount_options;
    int r;
    if ((r = parse_args(argc, argv, &options, &devicepath, &mountpath))) {
        return r;
    }

    if (options.verbose_mount) {
        printf("fs_mount: Mounting device [%s] on path [%s]\n", devicepath, mountpath);
    }

    int fd;
    if ((fd = open(devicepath, O_RDWR)) < 0) {
        fprintf(stderr, "Error opening block device\n");
        return -1;
    }
    disk_format_t df = detect_disk_format(fd);
    mx_status_t status = mount(fd, mountpath, df, &options, launch_logs_async);
    if (status != MX_OK) {
        fprintf(stderr, "fs_mount: Error while mounting: %d\n", status);
    }
    return status;
}
