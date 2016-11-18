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

bool verbose = false;

#define xprintf(fmt...) do { if (verbose) printf(fmt); } while(0)

int usage(void) {
    fprintf(stderr, "usage: umount [ <option>* ] path \n");
    fprintf(stderr, "   -v: Verbose mode\n");
    return -1;
}

int main(int argc, char** argv) {
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            verbose = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }
    if (argc < 2) {
        return usage();
    }
    char* path = argv[1];

    xprintf("Unmount path: %s\n", path);
    int fd = open(path, O_DIRECTORY | O_NOREMOTE);
    if (fd < 0) {
        fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
        return -1;
    }

    mx_status_t status = ioctl_devmgr_unmount_node(fd);
    if (status < 0) {
        fprintf(stderr, "Could not unmount filesystem: %d\n", status);
    } else {
        xprintf("Unmounted successfully\n");
    }
    close(fd);
    return status;
}
