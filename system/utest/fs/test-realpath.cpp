// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include "filesystems.h"

constexpr char kName[] = "::my_file";
constexpr char kTestNameDotDot[] = "::foo/../bar/../my_file";
constexpr char kTestNameDot[] = "::././././my_file";
constexpr char kTestNameBothDots[] = "::foo//.././/./././my_file";

static bool terminator(char c) { return c == 0 || c == '/'; }

static bool is_resolved(const char* path) {
    // Check that there are no ".", "//", or ".." components.
    // We assume there are no symlinks, since symlinks are not
    // yet supported on Fuchsia.
    while (true) {
        if (path[0] == 0) {
            return true;
        } else if (path[0] == '.' && terminator(path[1])) {
            return false;
        } else if (path[0] == '/' && path[1] == '/') {
            return false;
        } else if (path[0] == '.' && path[1] == '.' && terminator(path[2])) {
            return false;
        }
        if ((path = strchr(path, '/')) == NULL) {
            return true;
        }
        path += 1;
    }
}

bool test_realpath_absolute(void) {
    BEGIN_TEST;

    int fd = open(kName, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);

    struct stat sb;
    ASSERT_EQ(stat(kName, &sb), 0);

    // Find the real path of the file (since, due to linker magic, we
    // actually don't know it).
    char buf[PATH_MAX];
    ASSERT_EQ(realpath(kName, buf), buf);

    // Confirm that for (resolvable) cases of realpath, the name
    // can be cleaned.
    char buf2[PATH_MAX];
    ASSERT_EQ(realpath(kTestNameDotDot, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with ..) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    ASSERT_EQ(realpath(kTestNameDot, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with .) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    ASSERT_EQ(realpath(kTestNameBothDots, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with . and ..) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    // Clean up
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(kName), 0);
    END_TEST;
}

constexpr char kNameDir[] = "::my_dir";
constexpr char kNameFile[] = "::my_dir/my_file";
constexpr char kTestRelativeDotDot[] = "../my_dir/../my_dir/my_file";
constexpr char kTestRelativeDot[] = "./././my_file";
constexpr char kTestRelativeBothDots[] = "./..//my_dir/.././///././my_dir/./my_file";

bool test_realpath_relative(void) {
    BEGIN_TEST;

    ASSERT_EQ(mkdir(kNameDir, 0666), 0);
    int fd = open(kNameFile, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    close(fd);

    struct stat sb;
    ASSERT_EQ(stat(kNameFile, &sb), 0);

    // Find the real path of the file (since, due to linker magic, we
    // actually don't know it).
    char buf[PATH_MAX];
    ASSERT_EQ(realpath(kNameFile, buf), buf);

    char cwd[PATH_MAX];
    ASSERT_NONNULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_EQ(chdir(kNameDir), 0);

    char buf2[PATH_MAX];
    ASSERT_EQ(realpath(kTestRelativeDotDot, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with ..) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    ASSERT_EQ(realpath(kTestRelativeDot, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with .) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    ASSERT_EQ(realpath(kTestRelativeBothDots, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (with . and ..) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    // Test the longest possible path name

    // Extract the current working directory name ("my_dir/my_file" - "my_file")
    size_t cwd_len = strlen(buf) - strlen("my_file");
    char bufmax[PATH_MAX + 1];
    bufmax[0] = '.';
    size_t len = 1;
    // When realpath completes, it should return a result of the
    // form "CWD + '/' + "my_file".
    //
    // Ensure that our (uncanonicalized) path, including the CWD,
    // can fit within PATH_MAX (but just barely).
    while (len != PATH_MAX - cwd_len - strlen("my_file") - 1) {
        bufmax[len++] = '/';
    }
    memcpy(bufmax + len, "my_file", strlen("my_file"));
    bufmax[len + strlen("my_file")] = 0;
    ASSERT_EQ(strlen(bufmax), PATH_MAX - cwd_len - 1);

    ASSERT_EQ(realpath(bufmax, buf2), buf2);
    ASSERT_EQ(strcmp(buf, buf2), 0, "Name (longest path) did not resolve");
    ASSERT_TRUE(is_resolved(buf2));

    // Try a name that is too long (same as the last one, but just
    // add a single additional "/").
    bufmax[len++] = '/';
    strcpy(bufmax + len, "my_file");
    ASSERT_NULL(realpath(bufmax, buf2));

    // Clean up
    ASSERT_EQ(chdir(cwd), 0, "Could not return to original cwd");
    ASSERT_EQ(unlink(kNameFile), 0);
    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(realpath_tests,
    RUN_TEST_MEDIUM(test_realpath_absolute)
    RUN_TEST_MEDIUM(test_realpath_relative)
)
