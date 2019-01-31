// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <stdlib.h>
#include <blktest/blktest.h>
#include <unittest/unittest.h>

void print_usage(char* self) {
    fprintf(stderr, "Usage: %s -d <blkdev_path>\n", self);
}

int main(int argc, char** argv) {
    int opt;
    const char* blkdev = NULL;
    while ((opt = getopt(argc, argv, "d:")) != -1) {
        switch (opt) {
        case 'd':
            blkdev = optarg;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!blkdev) {
        print_usage(argv[0]);
        return 1;
    }

    unsetenv(BLKTEST_BLK_DEV);
    if (blkdev != NULL) {
        setenv(BLKTEST_BLK_DEV, blkdev, 1);
    }

    bool success = unittest_run_all_tests(argc, argv);

    unsetenv(BLKTEST_BLK_DEV);
    return success ? 0 : -1;
}
