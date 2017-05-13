// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <unittest/unittest.h>

#include "filesystems.h"

int main(int argc, char** argv) {
    use_real_disk = false;
    int i = 1;
    while (i < argc - 1) {
        if ((strlen(argv[i]) == 2) && (argv[i][0] == '-') && (argv[i][1] == 'd')) {
            if (strnlen(argv[i + 1], PATH_MAX) > 0) {
                strlcpy(test_disk_path, argv[i + 1], PATH_MAX);
                use_real_disk = true;
                break;
            }
        }
        i += 1;
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
