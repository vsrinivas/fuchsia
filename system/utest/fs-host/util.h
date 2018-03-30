// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <minfs/host.h>
#include <fcntl.h>

#define DEFAULT_DISK_SIZE (1llu << 32)

#define MOUNT_PATH "/tmp/zircon-fs-test"

typedef struct expected_dirent {
    bool seen; // Should be set to "false", used internally by checking function.
    const char* d_name;
    unsigned char d_type;
} expected_dirent_t;

void setup_fs_test(size_t disk_size);
void teardown_fs_test(void);
int run_fsck(void);

#define BEGIN_FS_TEST_CASE(case_name, disk_size) \
    BEGIN_TEST_CASE(case_name)                   \
    setup_fs_test(disk_size);

#define END_FS_TEST_CASE(case_name) \
    teardown_fs_test();             \
    END_TEST_CASE(case_name)

#define RUN_MINFS_TESTS(case_name, CASE_TESTS) \
    RUN_MINFS_TESTS_SIZE(case_name, DEFAULT_DISK_SIZE, CASE_TESTS)

#define RUN_MINFS_TESTS_SIZE(case_name, disk_size, CASE_TESTS) \
    BEGIN_FS_TEST_CASE(minfs_##case_name, disk_size)           \
    CASE_TESTS                                                 \
    END_FS_TEST_CASE(minfs_##case_name)

#define ASSERT_STREAM_ALL(op, fd, buf, len) \
    ASSERT_EQ(op(fd, (buf), (len)), (ssize_t)(len), "");
