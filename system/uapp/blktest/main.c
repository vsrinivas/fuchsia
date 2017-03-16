// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <blktest/blktest.h>
#include <unittest/unittest.h>

int main(int argc, char** argv) {
    const char* blkdev = NULL;
    int i = 1;
    while (i < argc - 1) {
        if ((strlen(argv[i]) == 2) && (argv[i][0] == '-') && (argv[i][1] == 'd')) {
            if (strlen(argv[i+1]) > 0) {
                blkdev = argv[i+1];
                break;
            }
        }
        i += 1;
    }

    unsetenv(BLKTEST_BLK_DEV);
    if (blkdev != NULL) {
        setenv(BLKTEST_BLK_DEV, blkdev, 1);
    }

    bool success = unittest_run_all_tests(argc, argv);

    unsetenv(BLKTEST_BLK_DEV);
    return success ? 0 : -1;
}
