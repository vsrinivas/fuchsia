// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

bool check_dir_contents(const char* dirname, expected_dirent_t* edirents, size_t len) {
    BEGIN_HELPER;
    DIR* dir = emu_opendir(dirname);

    emu_rewinddir(dir);
    size_t seen = 0;
    while (seen != len) {
        struct dirent* de = emu_readdir(dir);
        ASSERT_NE(de, (void*)0, "Didn't see all expected direntries");
        bool found = false;
        for (size_t i = 0; i < len; i++) {
            if (strcmp(edirents[i].d_name, de->d_name) == 0) {
                ASSERT_EQ(edirents[i].d_type, de->d_type, "Saw direntry with unexpected type");
                ASSERT_FALSE(edirents[i].seen, "Direntry seen twice");
                edirents[i].seen = true;
                seen++;
                found = true;
                break;
            }
        }

        ASSERT_TRUE(found, "Saw an unexpected dirent");
    }

    ASSERT_EQ(emu_readdir(dir), (void*)0, "There exists an entry we didn't expect to see");
    ASSERT_EQ(emu_closedir(dir), 0, "Couldn't close inspected directory");
    END_HELPER;
}

#define LARGE_PATH_LENGTH 128

bool test_directory_large(void) {
    BEGIN_TEST;

    const int num_files = 1024;
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
        int fd = emu_open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(emu_close(fd), 0);
    }

    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

bool test_directory_readdir(void) {
    BEGIN_TEST;

    ASSERT_EQ(emu_mkdir("::a", 0755), 0);
    ASSERT_EQ(emu_mkdir("::a", 0755), -1);

    expected_dirent_t empty_dir[] = {
        {false, ".", DT_DIR},
    };
    ASSERT_TRUE(check_dir_contents("::a", empty_dir, countof(empty_dir)));

    ASSERT_EQ(emu_mkdir("::a/dir1", 0755), 0);
    int fd = emu_open("::a/file1", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(emu_close(fd), 0);

    fd = emu_open("::a/file2", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(emu_close(fd), 0);

    ASSERT_EQ(emu_mkdir("::a/dir2", 0755), 0);
    expected_dirent_t filled_dir[] = {
        {false, ".", DT_DIR},
        {false, "dir1", DT_DIR},
        {false, "dir2", DT_DIR},
        {false, "file1", DT_REG},
        {false, "file2", DT_REG},
    };
    ASSERT_TRUE(check_dir_contents("::a", filled_dir, countof(filled_dir)));
    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

bool test_directory_readdir_large(void) {
    BEGIN_TEST;

    size_t num_entries = 1000;
    ASSERT_EQ(emu_mkdir("::dir", 0755), 0);

    for (size_t i = 0; i < num_entries; i++) {
        char dirname[100];
        snprintf(dirname, 100, "::dir/%05lu", i);
        ASSERT_EQ(emu_mkdir(dirname, 0755), 0);
    }

    DIR* dir = emu_opendir("::dir");
    ASSERT_NONNULL(dir);

    struct dirent* de;
    size_t num_seen = 0;
    size_t i = 0;
    while ((de = emu_readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        char dirname[100];
        snprintf(dirname, 100, "%05lu", i++);
        ASSERT_EQ(strcmp(de->d_name, dirname), 0, "Unexpected dirent");
        num_seen++;
    }

    ASSERT_EQ(num_seen, num_entries, "Did not see all expected entries");
    ASSERT_EQ(emu_closedir(dir), 0);
    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

RUN_MINFS_TESTS(directory_tests,
    RUN_TEST_LARGE(test_directory_large)
    RUN_TEST_MEDIUM(test_directory_readdir)
    RUN_TEST_MEDIUM(test_directory_readdir_large)
)
