// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fs-management/mount.h>
#include <magenta/compiler.h>
#include <unittest/unittest.h>

__BEGIN_CDECLS;

typedef struct fs_info {
    const char* name;
    bool (*exists)(void);
    int (*mkfs)(const char* disk_path);
    int (*mount)(const char* disk_path, const char* mount_path);
    int (*unmount)(const char* mount_path);
    int (*fsck)(const char* mount_path);
    bool can_be_mounted;
    bool can_mount_sub_filesystems;
    bool supports_hardlinks;
    bool supports_watchers;
    int64_t nsec_granularity;
} fs_info_t;

// Path to mounted filesystem currently being tested
#define MOUNT_PATH "/tmp/magenta-fs-test"
extern const char* test_root_path;

// Path to the mounted filesystem's backing store (if it exists)
extern char test_disk_path[];
// Is the disk path a REAL disk, or is it be a generated ramdisk?
extern bool use_real_disk;

// Current filesystem's info
extern fs_info_t* test_info;

extern const fsck_options_t test_fsck_options;

#define NUM_FILESYSTEMS 3
extern fs_info_t FILESYSTEMS[NUM_FILESYSTEMS];

int setup_fs_test(void);
int teardown_fs_test(void);

// As a small optimization, avoid even creating a ramdisk
// for filesystem tests when "utest_test_type" is not at
// LEAST size "medium". This avoids the overhead of creating
// a ramdisk when running small tests.
#define BEGIN_FS_TEST_CASE(case_name, fs_name, info)          \
        BEGIN_TEST_CASE(case_name##_##fs_name)                \
        if (utest_test_type & ~TEST_SMALL) {                  \
            test_info = info;                                 \
            if (test_info->exists()) {                        \
                setup_fs_test();

#define END_FS_TEST_CASE(case_name, fs_name)                  \
                teardown_fs_test();                           \
            } else {                                          \
                printf("Filesystem not found; not tested\n"); \
            }                                                 \
        }                                                     \
        END_TEST_CASE(case_name##_##fs_name)

#define RUN_FOR_ALL_FILESYSTEMS(case_name, CASE_TESTS)           \
        BEGIN_FS_TEST_CASE(case_name, memfs, &FILESYSTEMS[0])    \
        CASE_TESTS                                               \
        END_FS_TEST_CASE(case_name, memfs)                       \
        BEGIN_FS_TEST_CASE(case_name, minfs, &FILESYSTEMS[1])    \
        CASE_TESTS                                               \
        END_FS_TEST_CASE(case_name, minfs)                       \
        BEGIN_FS_TEST_CASE(case_name, thinfs, &FILESYSTEMS[2])   \
        CASE_TESTS                                               \
        END_FS_TEST_CASE(case_name, thinfs)

__END_CDECLS;
