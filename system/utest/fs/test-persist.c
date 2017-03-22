// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>

#include "filesystems.h"
#include "misc.h"

bool test_persist_simple(void) {
    if (!test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    BEGIN_TEST;

    const char* const paths[] = {"::abc", "::def", "::ghi", "::jkl", "::mnopqrstuvxyz"};
    for (size_t i = 0; i < countof(paths); i++) {
        int fd = open(paths[i], O_RDWR | O_CREAT | O_EXCL, 0644);
        ASSERT_GT(fd, 0, "");
        ASSERT_EQ(close(fd), 0, "");
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // The files should still exist when we remount
    for (size_t i = 0; i < countof(paths); i++) {
        ASSERT_EQ(unlink(paths[i]), 0, "");
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // But they should stay deleted!
    for (size_t i = 0; i < countof(paths); i++) {
        ASSERT_EQ(unlink(paths[i]), -1, "");
    }

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(persistence_tests,
    RUN_TEST_MEDIUM(test_persist_simple)
)
