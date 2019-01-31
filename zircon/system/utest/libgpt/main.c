// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>

bool gUseRamDisk = true;
char gDevPath[PATH_MAX];

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && (i + 1 < argc)) {
            snprintf(gDevPath, sizeof(gDevPath), "%s", argv[i + 1]);
            gUseRamDisk = false;
        } else {
            // Ignore options we don't recognize. See ulib/unittest/README.md.
            continue;
        }
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
