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

#include "misc.h"

void test_directory_filename_max(void) {
    printf("Test Directory (filename max)\n");

    // TODO(smklein): This value may be filesystem-specific. Plumb it through
    // from the test driver.
    int max_file_len = 255;
    char path[PATH_MAX + 1];

    // Unless the max_file_name is approaching PATH_MAX, this shouldn't be an
    // issue.
    assert(max_file_len + 3 /* '::' + '0' for 'too large' file */ < PATH_MAX);

    // Largest possible file length
    TRY(snprintf(path, sizeof(path), "::%0*d", max_file_len, 0x1337));
    int fd = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(close(fd));
    TRY(unlink(path));

    // Slightly too large file length
    TRY(snprintf(path, sizeof(path), "::%0*d", max_file_len + 1, 0xBEEF));
    EXPECT_FAIL(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
}

// Hopefuly not pushing against any 'max file length' boundaries, but large
// enough to fill a directory quickly.
#define LARGE_PATH_LENGTH 128

void test_directory_large(void) {
    printf("Test Directory (large)\n");

    // Write a bunch of files to a directory
    const int num_files = 1024;
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        int fd = TRY(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }

    // Unlink all those files
    for (int i = 0; i < num_files; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        TRY(unlink(path));
    }

    // TODO(smklein): Verify contents
}

void test_directory_max(void) {
    printf("Test Directory (max)\n");

    // Write the maximum number of files to a directory
    int i = 0;
    for (;; i++) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        if (i % 100 == 0) {
            printf(" Allocating: %s\n", path);
        }

        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            printf("    wrote %d direntries\n", i);
            break;
        }
        TRY(close(fd));
    }

    // Unlink all those files
    for (i -= 1; i >= 0; i--) {
        char path[LARGE_PATH_LENGTH + 1];
        TRY(snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i));
        TRY(unlink(path));
    }
}

void test_directory_coalesce_helper(const int* unlink_order) {
    const char* files[] = {
        "::coalesce/aaaaaaaa",
        "::coalesce/bbbbbbbb",
        "::coalesce/cccccccc",
        "::coalesce/dddddddd",
        "::coalesce/eeeeeeee",
    };
    int num_files = countof(files);

    // Allocate a bunch of files in a directory
    TRY(mkdir("::coalesce", 0755));
    for (int i = 0; i < num_files; i++) {
        int fd = TRY(open(files[i], O_RDWR | O_CREAT | O_EXCL, 0644));
        TRY(close(fd));
    }

    // Unlink all those files in the order specified
    for (int i = 0; i < num_files; i++) {
        assert(0 <= unlink_order[i] && unlink_order[i] < num_files);
        TRY(unlink(files[unlink_order[i]]));
    }

    TRY(unlink("::coalesce"));
}

void test_directory_coalesce(void) {
    printf("Test Directory (coalesce)\n");

    // Test some cases of coalescing, assuming the directory was filled
    // according to allocation order. If it wasn't, this test should still pass,
    // but there is no mechanism to check the "location of a direntry in a
    // directory", so this is our best shot at "poking" the filesystem to try to
    // coalesce.

    // Case 1: Test merge-with-left
    printf("  Test merge-with-left\n");
    const int merge_with_left[] = {0, 1, 2, 3, 4};
    test_directory_coalesce_helper(merge_with_left);

    // Case 2: Test merge-with-right
    printf("  Test merge-with-right\n");
    const int merge_with_right[] = {4, 3, 2, 1, 0};
    test_directory_coalesce_helper(merge_with_right);

    // Case 3: Test merge-with-both
    printf("  Test merge-with-both\n");
    const int merge_with_both[] = {1, 3, 2, 0, 4};
    test_directory_coalesce_helper(merge_with_both);
}

void test_directory_trailing_slash(void) {
    printf("Test Directory Trailing Slash\n");

    // We should be able to refer to directories with any number of trailing
    // slashes, and still refer to the same entity.
    TRY(mkdir("::a", 0755));
    TRY(mkdir("::b/", 0755));
    TRY(mkdir("::c//", 0755));
    TRY(mkdir("::d///", 0755));

    TRY(unlink("::a///"));
    TRY(unlink("::b//"));
    TRY(unlink("::c/"));

    // Before we unlink 'd', try renaming it using some trailing '/' characters.
    TRY(rename("::d", "::e"));
    TRY(rename("::e", "::d/"));
    TRY(rename("::d/", "::e"));
    TRY(rename("::e/", "::d/"));
    TRY(unlink("::d"));

    // We can make / unlink a file...
    TRY(close(TRY(open("::a", O_RDWR | O_CREAT | O_EXCL, 0644))));
    TRY(unlink("::a"));

    // ... But we cannot refer to that file using a trailing '/'.
    TRY(close(TRY(open("::a", O_RDWR | O_CREAT | O_EXCL, 0644))));
    EXPECT_FAIL(open("::a/", O_RDWR, 0644));

    // We can rename the file...
    TRY(rename("::a", "::b"));
    // ... But neither the source (nor the destination) can have trailing slashes.
    EXPECT_FAIL(rename("::b", "::a/"));
    EXPECT_FAIL(rename("::b/", "::a"));
    EXPECT_FAIL(rename("::b/", "::a/"));
    EXPECT_FAIL(unlink("::b/"));
    TRY(unlink("::b"));
}

typedef struct expected_dirent {
    bool seen; // Flip to true once it has been seen
    const char* d_name;
    unsigned char d_type;
} expected_dirent_t;

void check_contains_all(const char* dirname, expected_dirent_t* edirents, size_t len) {
    DIR* dir = opendir(dirname);
    assert(dir != NULL);
    size_t seen = 0;

    while (seen != len) {
        struct dirent* de = readdir(dir);
        assert(de != NULL); // Terminated before seeing all the direntries we expected to see
        bool found = false;
        for (size_t i = 0; i < len; i++) {
            if (strcmp(edirents[i].d_name, de->d_name) == 0) {
                assert(edirents[i].d_type == de->d_type);
                assert(!edirents[i].seen);
                edirents[i].seen = true;
                seen++;
                found = true;
                break;
            }
        }
        if (!found) {
            printf("Saw an unexpected dirent: %s\n", de->d_name);
            assert(false);
        }
    }

    assert(readdir(dir) == NULL); // There exists an entry we didn't expect to see
    closedir(dir);

    // Flip 'seen' back to false so the array of expected dirents can be reused
    for (size_t i = 0; i < len; i++) {
        edirents[i].seen = false;
    }
}

void test_directory_readdir(void) {
    printf("Test Directory Readdir\n");
    TRY(mkdir("::a", 0755));
    EXPECT_FAIL(mkdir("::a", 0755));

    expected_dirent_t empty_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
    };
    check_contains_all("::a", empty_dir, countof(empty_dir));

    TRY(mkdir("::a/dir1", 0755));
    TRY(close(TRY(open("::a/file1", O_RDWR | O_CREAT | O_EXCL, 0644))));
    TRY(close(TRY(open("::a/file2", O_RDWR | O_CREAT | O_EXCL, 0644))));
    TRY(mkdir("::a/dir2", 0755));
    expected_dirent_t filled_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "dir1", DT_DIR},
        {false, "dir2", DT_DIR},
        {false, "file1", DT_REG},
        {false, "file2", DT_REG},
    };
    check_contains_all("::a", filled_dir, countof(filled_dir));

    TRY(unlink("::a/dir2"));
    TRY(unlink("::a/file2"));
    expected_dirent_t partial_dir[] = {
        {false, ".", DT_DIR},
        {false, "..", DT_DIR},
        {false, "dir1", DT_DIR},
        {false, "file1", DT_REG},
    };
    check_contains_all("::a", partial_dir, countof(partial_dir));

    TRY(unlink("::a/dir1"));
    TRY(unlink("::a/file1"));
    check_contains_all("::a", empty_dir, countof(empty_dir));
}

int test_directory(void) {
    test_directory_coalesce();
    test_directory_filename_max();
    test_directory_large();
    test_directory_trailing_slash();
    test_directory_readdir();
// TODO(smklein): Run this when MemFS can execute it without causing an OOM
#if 0
    test_directory_max();
#endif
    return 0;
}
