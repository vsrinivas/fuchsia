// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <fvm/fvm.h>
#include <unittest/unittest.h>
#include <zircon/device/device.h>

#include "filesystems.h"

const char* filesystem_name_filter = "";

int usage(char** argv) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "$ %s [OPTIONS]\n", argv[0]);
    fprintf(stderr, " -d blkdev : Use block device |blkdev| instead of a ramdisk\n");
    fprintf(stderr, " -f fs     : Test only fileystem |fs|, where |fs| is one of:\n");
    for (int j = 0; j < NUM_FILESYSTEMS; j++) {
        fprintf(stderr, "   %s\n", FILESYSTEMS[j].name);
    }
    return -1;
}

int main(int argc, char** argv) {
    use_real_disk = false;

    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-d") && (i + 1 < argc)) {
            int fd = open(argv[i + 1], O_RDWR);
            if (fd < 0) {
                fprintf(stderr, "[fs] Could not open block device\n");
                return -1;
            } else if (ioctl_device_get_topo_path(fd, test_disk_path, PATH_MAX) < 0) {
                fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
                return -1;
            }
            // If we previously tried running tests on this disk, it may
            // have created an FVM and failed. (Try to) clean up from previous state
            // before re-running.
            fvm_destroy(test_disk_path);
            close(fd);
            use_real_disk = true;
            i += 2;
        } else if (!strcmp(argv[i], "-f") && (i + 1 < argc)) {
            bool found = false;
            for (int j = 0; j < NUM_FILESYSTEMS; j++) {
                if (!strcmp(argv[i + 1], FILESYSTEMS[j].name)) {
                    found = true;
                    filesystem_name_filter = argv[i + 1];
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "error: Filesystem not found\n");
                return usage(argv);
            }
            i += 2;
        } else {
            return usage(argv);
        }
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
