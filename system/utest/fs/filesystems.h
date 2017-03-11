// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unittest/unittest.h>

#define ASSERT_STREAM_ALL(op, fd, buf, len) \
    ASSERT_EQ(op(fd, (buf), (len)), (ssize_t)(len), "");

typedef struct fs_info {
    const char* name;
    int (*mkfs)(const char* disk_path);
    int (*mount)(const char* disk_path, const char* mount_path);
    int (*unmount)(const char* mount_path);
    bool can_be_mounted;
    bool can_mount_sub_filesystems;
    bool supports_hardlinks;
} fs_info_t;

// Path to mounted filesystem currently being tested
#define MOUNT_PATH "/tmp/magenta-fs-test"
extern const char* test_root_path;

// Path to the mounted filesystem's backing store (if it exists)
extern char test_disk_path[];

// Current filesystem's info
extern fs_info_t* test_info;

#define NUM_FILESYSTEMS 2
extern fs_info_t FILESYSTEMS[NUM_FILESYSTEMS];

int create_ramdisk(const char* ramdisk_name, char* ramdisk_path_out);
int destroy_ramdisk(const char* ramdisk_path);

int setup_fs_test(void);
int teardown_fs_test(void);

// As a small optimization, avoid even creating a ramdisk
// for filesystem tests when "utest_test_type" is not at
// LEAST size "medium". This avoids the overhead of creating
// a ramdisk when running small tests.
#define BEGIN_FS_TEST_CASE(case_name, fs_name, info) \
        BEGIN_TEST_CASE(case_name##_##fs_name)       \
        if (utest_test_type & ~TEST_SMALL) {         \
            test_info = info;                        \
            setup_fs_test();

#define END_FS_TEST_CASE(case_name, fs_name)         \
            teardown_fs_test();                      \
        }                                            \
        END_TEST_CASE(case_name##_##fs_name)

#define RUN_FOR_ALL_FILESYSTEMS(case_name, CASE_TESTS)           \
        BEGIN_FS_TEST_CASE(case_name, memfs, &FILESYSTEMS[0])    \
        CASE_TESTS                                               \
        END_FS_TEST_CASE(case_name, memfs)                       \
        BEGIN_FS_TEST_CASE(case_name, minfs, &FILESYSTEMS[1])    \
        CASE_TESTS                                               \
        END_FS_TEST_CASE(case_name, minfs)
