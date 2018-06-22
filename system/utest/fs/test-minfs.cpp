// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for MinFS-specific behavior.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <minfs/format.h>
#include <unittest/unittest.h>
#include <zircon/device/vfs.h>

#include "filesystems.h"

namespace {

bool QueryInfo(size_t expected_nodes) {
    int fd = open(kMountPath, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);

    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t rv = ioctl_vfs_query_fs(fd, info, sizeof(buf) - 1);
    ASSERT_EQ(close(fd), 0);

    ASSERT_EQ(rv, sizeof(vfs_query_info_t) + strlen("minfs"), "Failed to query filesystem");

    buf[rv] = '\0';  // NULL terminate the name.
    ASSERT_EQ(strncmp("minfs", info->name, strlen("minfs")), 0);
    ASSERT_EQ(info->block_size, minfs::kMinfsBlockSize);
    ASSERT_EQ(info->max_filename_size, minfs::kMinfsMaxNameSize);
    ASSERT_EQ(info->fs_type, VFS_TYPE_MINFS);
    ASSERT_NE(info->fs_id, 0);

    ASSERT_EQ(info->total_bytes, 8 * 1024 * 1024);

    // TODO(ZX-1372): Adjust this once minfs accounting on truncate is fixed.
    ASSERT_EQ(info->used_bytes, 2 * minfs::kMinfsBlockSize);
    ASSERT_EQ(info->total_nodes, 32 * 1024);
    ASSERT_EQ(info->used_nodes, expected_nodes + 2);
    return true;
}

}  // namespace

bool TestQueryInfo(void) {
    BEGIN_TEST;

    ASSERT_TRUE(QueryInfo(0));
    for (int i = 0; i < 16; i++) {
        char path[128];
        snprintf(path, sizeof(path) - 1, "%s/file_%d", kMountPath, i);

        int fd = open(path, O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0, "Failed to create file");
        ASSERT_EQ(ftruncate(fd, 30 * 1024), 0);
        ASSERT_EQ(close(fd), 0);
    }

    ASSERT_TRUE(QueryInfo(16));
    END_TEST;
}

#define RUN_MINFS_TESTS(name, CASE_TESTS) \
    FS_TEST_CASE(name, default_test_disk, CASE_TESTS, FS_TEST_FVM, minfs, 1)

RUN_MINFS_TESTS(FsMinfsTestsFvm,
    RUN_TEST_MEDIUM(TestQueryInfo)
)
