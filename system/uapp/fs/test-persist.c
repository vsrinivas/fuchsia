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

void test_persist_simple(fs_info_t* info) {
    printf("Test Persist (simple)\n");
    const char* const paths[] = {"::abc", "::def", "::ghi", "::jkl", "::mnopqrstuvxyz"};
    for (size_t i = 0; i < countof(paths); i++) {
        int fd = TRY(open(paths[i], O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }

    TRY(info->unmount(test_root_path));
    TRY(info->mount(test_disk_path, test_root_path));

    // The files should still exist when we remount
    for (size_t i = 0; i < countof(paths); i++) {
        TRY(unlink(paths[i]));
    }

    TRY(info->unmount(test_root_path));
    TRY(info->mount(test_disk_path, test_root_path));

    // But they should stay deleted!
    for (size_t i = 0; i < countof(paths); i++) {
        EXPECT_FAIL(unlink(paths[i]));
    }
}

int test_persist(fs_info_t* info) {
    if (!info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return 0;
    }
    test_persist_simple(info);
    return 0;
}
