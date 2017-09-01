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
    bool supports_create_by_vmo;
    bool supports_mmap;
    bool supports_resize;
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

typedef enum fs_test_type {
    // The partition may appear as any generic block device
    FS_TEST_NORMAL,
    // The partition should appear on top of a resizable
    // FVM device
    FS_TEST_FVM,
} fs_test_type_t;

void setup_fs_test(size_t disk_size, fs_test_type_t test_class);
void teardown_fs_test(fs_test_type_t test_class);

inline bool can_execute_test(fs_info_t* info, fs_test_type_t t) {
    switch (t) {
    case FS_TEST_NORMAL:
        return info->exists();
    case FS_TEST_FVM:
        return info->exists() && info->supports_resize;
    }
    return false;
}

// As a small optimization, avoid even creating a ramdisk
// for filesystem tests when "utest_test_type" is not at
// LEAST size "medium". This avoids the overhead of creating
// a ramdisk when running small tests.
#define BEGIN_FS_TEST_CASE(case_name, dsize, fs_type, fs_name, info) \
    BEGIN_TEST_CASE(case_name##_##fs_name)                           \
    if (utest_test_type & ~TEST_SMALL) {                             \
        test_info = info;                                            \
        if (can_execute_test(test_info, fs_type)) {                  \
            setup_fs_test(dsize, fs_type);

#define END_FS_TEST_CASE(case_name, fs_type, fs_name) \
            teardown_fs_test(fs_type);                \
        } else {                                      \
            printf("Filesystem not tested\n");        \
        }                                             \
    }                                                 \
    END_TEST_CASE(case_name##_##fs_name)

#define FS_TEST_CASE(case_name, dsize, CASE_TESTS, test_type, fs_type, index)     \
    BEGIN_FS_TEST_CASE(case_name, dsize, test_type, fs_type, &FILESYSTEMS[index]) \
    CASE_TESTS                                                                    \
    END_FS_TEST_CASE(case_name, test_type, fs_type)

#define DEFAULT_DISK_SIZE (1llu << 32)

#define RUN_FOR_ALL_FILESYSTEMS_TYPE(case_name, test_type, CASE_TESTS)           \
    FS_TEST_CASE(case_name, DEFAULT_DISK_SIZE, CASE_TESTS, test_type, memfs, 0)  \
    FS_TEST_CASE(case_name, DEFAULT_DISK_SIZE, CASE_TESTS, test_type, minfs, 1)  \
    FS_TEST_CASE(case_name, DEFAULT_DISK_SIZE, CASE_TESTS, test_type, thinfs, 2)

#define RUN_FOR_ALL_FILESYSTEMS_SIZE(case_name, dsize, CASE_TESTS)          \
    FS_TEST_CASE(case_name, dsize, CASE_TESTS, FS_TEST_NORMAL, memfs, 0)    \
    FS_TEST_CASE(case_name, dsize, CASE_TESTS, FS_TEST_NORMAL, minfs, 1)    \
    FS_TEST_CASE(case_name##_fvm, dsize, CASE_TESTS, FS_TEST_FVM, minfs, 1) \
    FS_TEST_CASE(case_name, dsize, CASE_TESTS, FS_TEST_NORMAL, thinfs, 2)

#define RUN_FOR_ALL_FILESYSTEMS(case_name, CASE_TESTS)                     \
    RUN_FOR_ALL_FILESYSTEMS_SIZE(case_name, DEFAULT_DISK_SIZE, CASE_TESTS)

__END_CDECLS;
