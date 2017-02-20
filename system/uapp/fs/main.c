// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>

#include "filesystems.h"
#include "misc.h"

int test_attr(fs_info_t* info);
int test_append(fs_info_t* info);
int test_basic(fs_info_t* info);
int test_link(fs_info_t* info);
int test_directory(fs_info_t* info);
int test_maxfile(fs_info_t* info);
int test_overflow(fs_info_t* info);
int test_persist(fs_info_t* info);
int test_rw_workers(fs_info_t* info);
int test_random_op_multithreaded(fs_info_t* info);
int test_rename(fs_info_t* info);
int test_sync(fs_info_t* info);
int test_truncate(fs_info_t* info);
int test_unlink(fs_info_t* info);

struct {
    const char* name;
    int (*test)(fs_info_t* info);
} FS_TESTS[] = {
    {"attr", test_attr},
    {"append", test_append},
    {"basic", test_basic},
    {"link", test_link},
    {"directory", test_directory},
    {"maxfile", test_maxfile},
    {"overflow", test_overflow},
    {"persist", test_persist},
    {"rw_workers", test_rw_workers},
    {"random_op_multithreaded", test_random_op_multithreaded},
    {"rename", test_rename},
    {"sync", test_sync},
    {"truncate", test_truncate},
    {"unlink", test_unlink},
};

void run_fs_tests(fs_info_t* info, int argc, char** argv) {
    fprintf(stderr, "--- fs tests ---\n");
    for (unsigned i = 0; i < countof(FS_TESTS); i++) {
        if (argc > 1 && strcmp(argv[1], FS_TESTS[i].name)) {
            continue;
        }
        fprintf(stderr, "Running Test: %s\n", FS_TESTS[i].name);

        if (create_ramdisk("fs-test-ramdisk", test_disk_path)) {
            fprintf(stderr, "FAILED: Could not create ramdisk for test\n");
            exit(-1);
        }
        if (info->mkfs(test_disk_path)) {
            fprintf(stderr, "FAILED: Could not format ramdisk for test\n");
            exit(-1);
        }

        if (info->mount(test_disk_path, test_root_path)) {
            fprintf(stderr, "FAILED: Error mounting filesystem\n");
            exit(-1);
        }

        if (FS_TESTS[i].test(info)) {
            fprintf(stderr, "FAILED: %s\n", FS_TESTS[i].name);
            exit(-1);
        } else {
            fprintf(stderr, "PASSED: %s\n", FS_TESTS[i].name);
        }

        if (info->unmount(test_root_path)) {
            fprintf(stderr, "FAILED: Error unmounting filesystem\n");
            exit(-1);
        }

        if (destroy_ramdisk(test_disk_path)) {
            fprintf(stderr, "FAILED: Error destroying ramdisk\n");
            exit(-1);
        }
    }
}

#define MOUNT_PATH "/tmp/magenta-fs-test"

int main(int argc, char** argv) {
    test_root_path = MOUNT_PATH;
    int r = mkdir(test_root_path, 0755);
    if ((r < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystem\n");
        return -1;
    }

    for (unsigned i = 0; i < countof(FILESYSTEMS); i++) {
        printf("Testing FS: %s\n", FILESYSTEMS[i].name);
        run_fs_tests(&FILESYSTEMS[i], argc, argv);
    }
    return 0;
}
