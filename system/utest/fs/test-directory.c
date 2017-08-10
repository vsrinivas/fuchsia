// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>

#include "filesystems.h"
#include "misc.h"

bool test_directory_filename_max(void) {
    BEGIN_TEST;

    // TODO(smklein): This value may be filesystem-specific. Plumb it through
    // from the test driver.
    int max_file_len = 255;
    char path[PATH_MAX + 1];

    // Unless the max_file_name is approaching PATH_MAX, this shouldn't be an
    // issue.
    assert(max_file_len + 3 /* '::' + '0' for 'too large' file */ < PATH_MAX);

    // Largest possible file length
    snprintf(path, sizeof(path), "::%0*d", max_file_len, 0x1337);
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(path), 0, "");

    // Slightly too large file length
    snprintf(path, sizeof(path), "::%0*d", max_file_len + 1, 0xBEEF);
    ASSERT_EQ(open(path, O_RDWR | O_CREAT | O_EXCL, 0644), -1, "");

    END_TEST;
}

// Hopefuly not pushing against any 'max file length' boundaries, but large
// enough to fill a directory quickly.
#define LARGE_PATH_LENGTH 128

bool test_directory_large(void) {
    BEGIN_TEST;

    // Write a bunch of files to a directory
    const int num_files = 1024;
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        ASSERT_GT(fd, 0, "");
        ASSERT_EQ(close(fd), 0, "");
    }

    // Unlink all those files
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
        ASSERT_EQ(unlink(path), 0, "");
    }

    // TODO(smklein): Verify contents

    END_TEST;
}

bool test_directory_max(void) {
    BEGIN_TEST;

    // Write the maximum number of files to a directory
    int i = 0;
    for (;; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
        if (i % 100 == 0) {
            printf(" Allocating: %s\n", path);
        }

        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            printf("    wrote %d direntries\n", i);
            break;
        }
        ASSERT_EQ(close(fd), 0, "");
    }

    // Unlink all those files
    for (i -= 1; i >= 0; i--) {
        char path[LARGE_PATH_LENGTH + 1];
        snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
        ASSERT_EQ(unlink(path), 0, "");
    }

    END_TEST;
}

bool test_directory_coalesce_helper(const int* unlink_order) {
    const char* files[] = {
        "::coalesce/aaaaaaaa",
        "::coalesce/bbbbbbbb",
        "::coalesce/cccccccc",
        "::coalesce/dddddddd",
        "::coalesce/eeeeeeee",
    };
    int num_files = countof(files);

    // Allocate a bunch of files in a directory
    ASSERT_EQ(mkdir("::coalesce", 0755), 0, "");
    for (int i = 0; i < num_files; i++) {
        int fd = open(files[i], O_RDWR | O_CREAT | O_EXCL, 0644);
        ASSERT_GT(fd, 0, "");
        ASSERT_EQ(close(fd), 0, "");
    }

    // Unlink all those files in the order specified
    for (int i = 0; i < num_files; i++) {
        assert(0 <= unlink_order[i] && unlink_order[i] < num_files);
        ASSERT_EQ(unlink(files[unlink_order[i]]), 0, "");
    }

    ASSERT_EQ(rmdir("::coalesce"), 0, "");
    return true;
}

bool test_directory_coalesce(void) {
    BEGIN_TEST;

    // Test some cases of coalescing, assuming the directory was filled
    // according to allocation order. If it wasn't, this test should still pass,
    // but there is no mechanism to check the "location of a direntry in a
    // directory", so this is our best shot at "poking" the filesystem to try to
    // coalesce.

    // Case 1: Test merge-with-left
    const int merge_with_left[] = {0, 1, 2, 3, 4};
    ASSERT_TRUE(test_directory_coalesce_helper(merge_with_left), "");

    // Case 2: Test merge-with-right
    const int merge_with_right[] = {4, 3, 2, 1, 0};
    ASSERT_TRUE(test_directory_coalesce_helper(merge_with_right), "");

    // Case 3: Test merge-with-both
    const int merge_with_both[] = {1, 3, 2, 0, 4};
    ASSERT_TRUE(test_directory_coalesce_helper(merge_with_both), "");

    END_TEST;
}

bool test_directory_trailing_slash(void) {
    BEGIN_TEST;

    // We should be able to refer to directories with any number of trailing
    // slashes, and still refer to the same entity.
    ASSERT_EQ(mkdir("::a", 0755), 0, "");
    ASSERT_EQ(mkdir("::b/", 0755), 0, "");
    ASSERT_EQ(mkdir("::c//", 0755), 0, "");
    ASSERT_EQ(mkdir("::d///", 0755), 0, "");

    ASSERT_EQ(rmdir("::a///"), 0, "");
    ASSERT_EQ(rmdir("::b//"), 0, "");
    ASSERT_EQ(rmdir("::c/"), 0, "");

    // Before we unlink 'd', try renaming it using some trailing '/' characters.
    ASSERT_EQ(rename("::d", "::e"), 0, "");
    ASSERT_EQ(rename("::e", "::d/"), 0, "");
    ASSERT_EQ(rename("::d/", "::e"), 0, "");
    ASSERT_EQ(rename("::e/", "::d/"), 0, "");
    ASSERT_EQ(rmdir("::d"), 0, "");

    // We can make / unlink a file...
    int fd = open("::a", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("::a"), 0, "");

    // ... But we cannot refer to that file using a trailing '/'.
    fd = open("::a", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(open("::a/", O_RDWR, 0644), -1, "");

    // We can rename the file...
    ASSERT_EQ(rename("::a", "::b"), 0, "");
    // ... But neither the source (nor the destination) can have trailing slashes.
    ASSERT_EQ(rename("::b", "::a/"), -1, "");
    ASSERT_EQ(rename("::b/", "::a"), -1, "");
    ASSERT_EQ(rename("::b/", "::a/"), -1, "");
    ASSERT_EQ(unlink("::b/"), -1, "");

    ASSERT_EQ(unlink("::b"), 0, "");

    END_TEST;
}

bool test_directory_readdir(void) {
    BEGIN_TEST;

    ASSERT_EQ(mkdir("::a", 0755), 0, "");
    ASSERT_EQ(mkdir("::a", 0755), -1, "");

    expected_dirent_t empty_dir[] = {
        {false, ".", DT_DIR},
    };
    ASSERT_TRUE(check_dir_contents("::a", empty_dir, countof(empty_dir)), "");

    ASSERT_EQ(mkdir("::a/dir1", 0755), 0, "");
    int fd = open("::a/file1", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");

    fd = open("::a/file2", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");

    ASSERT_EQ(mkdir("::a/dir2", 0755), 0, "");
    expected_dirent_t filled_dir[] = {
        {false, ".", DT_DIR},
        {false, "dir1", DT_DIR},
        {false, "dir2", DT_DIR},
        {false, "file1", DT_REG},
        {false, "file2", DT_REG},
    };
    ASSERT_TRUE(check_dir_contents("::a", filled_dir, countof(filled_dir)), "");

    ASSERT_EQ(rmdir("::a/dir2"), 0, "");
    ASSERT_EQ(unlink("::a/file2"), 0, "");
    expected_dirent_t partial_dir[] = {
        {false, ".", DT_DIR},
        {false, "dir1", DT_DIR},
        {false, "file1", DT_REG},
    };
    ASSERT_TRUE(check_dir_contents("::a", partial_dir, countof(partial_dir)), "");

    ASSERT_EQ(rmdir("::a/dir1"), 0, "");
    ASSERT_EQ(unlink("::a/file1"), 0, "");
    ASSERT_TRUE(check_dir_contents("::a", empty_dir, countof(empty_dir)), "");
    ASSERT_EQ(unlink("::a"), 0, "");

    END_TEST;
}

// Create a directory named "::dir" with entries "00000", "00001" ... up to
// num_entries.
bool large_dir_setup(size_t num_entries) {
    ASSERT_EQ(mkdir("::dir", 0755), 0, "");

    // Create a large directory (ideally, large enough that our libc
    // implementation can't cache the entire contents of the directory
    // with one 'getdirents' call).
    for (size_t i = 0; i < num_entries; i++) {
        char dirname[100];
        snprintf(dirname, 100, "::dir/%05lu", i);
        ASSERT_EQ(mkdir(dirname, 0755), 0, "");
    }

    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir, "");

    // As a sanity check, it should contain all then entries we made
    struct dirent* de;
    size_t num_seen = 0;
    size_t i = 0;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            // Ignore these entries
            continue;
        }
        char dirname[100];
        snprintf(dirname, 100, "%05lu", i++);
        ASSERT_EQ(strcmp(de->d_name, dirname), 0, "Unexpected dirent");
        num_seen++;
    }
    ASSERT_EQ(closedir(dir), 0, "");

    return true;
}

bool test_directory_readdir_rm_all(void) {
    BEGIN_TEST;

    size_t num_entries = 1000;
    ASSERT_TRUE(large_dir_setup(num_entries), "");

    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir, "");

    // Unlink all the entries as we read them.
    struct dirent* de;
    size_t num_seen = 0;
    size_t i = 0;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            // Ignore these entries
            continue;
        }
        char dirname[100];
        snprintf(dirname, 100, "%05lu", i++);
        ASSERT_EQ(strcmp(de->d_name, dirname), 0, "Unexpected dirent");
        ASSERT_EQ(unlinkat(dirfd(dir), dirname, AT_REMOVEDIR), 0, "");
        num_seen++;
    }

    ASSERT_EQ(num_seen, num_entries, "Did not see all expected entries");
    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(rmdir("::dir"), 0, "Could not unlink containing directory");
    END_TEST;
}

bool test_directory_rewind(void) {
    BEGIN_TEST;

    ASSERT_EQ(mkdir("::a", 0755), 0, "");
    expected_dirent_t empty_dir[] = {
        {false, ".", DT_DIR},
    };

    DIR* dir = opendir("::a");
    ASSERT_NE(dir, NULL, "");

    // We should be able to repeatedly access the directory without
    // re-opening it.
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");

    ASSERT_EQ(mkdirat(dirfd(dir), "b", 0755), 0, "");
    ASSERT_EQ(mkdirat(dirfd(dir), "c", 0755), 0, "");

    // We should be able to modify the directory and re-process it without
    // re-opening it.
    expected_dirent_t dir_contents[] = {
        {false, ".", DT_DIR},
        {false, "b", DT_DIR},
        {false, "c", DT_DIR},
    };
    ASSERT_TRUE(fcheck_dir_contents(dir, dir_contents, countof(dir_contents)), "");
    ASSERT_TRUE(fcheck_dir_contents(dir, dir_contents, countof(dir_contents)), "");

    ASSERT_EQ(rmdir("::a/b"), 0, "");
    ASSERT_EQ(rmdir("::a/c"), 0, "");

    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");

    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(rmdir("::a"), 0, "");

    END_TEST;
}

bool test_directory_after_rmdir(void) {
    BEGIN_TEST;

    expected_dirent_t empty_dir[] = {
        {false, ".", DT_DIR},
    };

    // Make a directory...
    ASSERT_EQ(mkdir("::dir", 0755), 0, "");
    DIR* dir = opendir("::dir");
    ASSERT_NE(dir, NULL, "");
    // We can make and delete subdirectories, since "::dir" exists...
    ASSERT_EQ(mkdir("::dir/subdir", 0755), 0, "");
    ASSERT_EQ(rmdir("::dir/subdir"), 0, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");

    // Remove the directory. It's still open, so it should appear empty.
    ASSERT_EQ(rmdir("::dir"), 0, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");

    // But we can't make new files / directories, by path...
    ASSERT_EQ(mkdir("::dir/subdir", 0755), -1, "");
    // ... Or with the open fd.
    int fd = dirfd(dir);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(openat(fd, "file", O_CREAT | O_RDWR), -1, "Can't make new files in deleted dirs");
    ASSERT_EQ(mkdirat(fd, "dir", 0755), -1, "Can't make new files in deleted dirs");

    // In fact, the "dir" path should still be usable, even as a file!
    fd = open("::dir", O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0, "");
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("::dir"), 0, "");

    // After all that, dir still looks like an empty directory...
    ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, countof(empty_dir)), "");
    ASSERT_EQ(closedir(dir), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(directory_tests,
    RUN_TEST_MEDIUM(test_directory_coalesce)
    RUN_TEST_MEDIUM(test_directory_filename_max)
    RUN_TEST_LARGE(test_directory_large)
    RUN_TEST_MEDIUM(test_directory_trailing_slash)
    RUN_TEST_MEDIUM(test_directory_readdir)
    RUN_TEST_MEDIUM(test_directory_readdir_rm_all)
    RUN_TEST_MEDIUM(test_directory_rewind)
    RUN_TEST_MEDIUM(test_directory_after_rmdir)
)

// TODO(smklein): Run this when MemFS can execute it without causing an OOM
#if 0
    RUN_TEST_LARGE(test_directory_max)
#endif
