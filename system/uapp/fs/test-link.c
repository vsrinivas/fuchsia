// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

// Check the contents of a file are what we expect
void confirm_contents(int fd, uint8_t* buf, size_t length) {
    TRY(lseek(fd, 0, SEEK_SET));
    uint8_t* out = malloc(length);
    assert(out != NULL);
    ssize_t r = TRY(read(fd, out, length));
    assert((size_t) r == length);
    assert(memcmp(buf, out, length) == 0);
    free(out);
}

void test_link_basic(void) {
    printf("Test Link (basic)\n");

    const char* oldpath = "::a";
    const char* newpath = "::b";

    // Make a file, fill it with content
    int fd = TRY(open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644));
    uint8_t buf[100];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t) rand();
    }
    ssize_t r = TRY(write(fd, buf, sizeof(buf)));
    assert(r == sizeof(buf));
    confirm_contents(fd, buf, sizeof(buf));

    TRY(link(oldpath, newpath));

    // Confirm that both the old link and the new links exist
    int fd2 = TRY(open(newpath, O_RDONLY, 0644));
    confirm_contents(fd2, buf, sizeof(buf));
    confirm_contents(fd, buf, sizeof(buf));

    // Remove the old link
    TRY(close(fd));
    TRY(close(fd2));
    TRY(unlink(oldpath));

    // Open the link by its new name, and verify that the contents have
    // not been altered by the removal of the old link.
    fd = TRY(open(newpath, O_RDONLY, 0644));
    confirm_contents(fd, buf, sizeof(buf));

    TRY(close(fd));
    TRY(unlink(newpath));
}

void test_link_between_dirs(void) {
    printf("Test Link (between dirs)\n");

    TRY(mkdir("::dira", 0755));
    TRY(mkdir("::dirb", 0755));
    const char* oldpath = "::dira/a";
    const char* newpath = "::dirb/b";

    // Make a file, fill it with content
    int fd = TRY(open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644));
    uint8_t buf[100];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t) rand();
    }
    ssize_t r = TRY(write(fd, buf, sizeof(buf)));
    assert(r == sizeof(buf));
    confirm_contents(fd, buf, sizeof(buf));

    TRY(link(oldpath, newpath));

    // Confirm that both the old link and the new links exist
    int fd2 = TRY(open(newpath, O_RDWR, 0644));
    confirm_contents(fd2, buf, sizeof(buf));
    confirm_contents(fd, buf, sizeof(buf));

    // Remove the old link
    TRY(close(fd));
    TRY(close(fd2));
    TRY(unlink(oldpath));

    // Open the link by its new name
    fd = TRY(open(newpath, O_RDWR, 0644));
    confirm_contents(fd, buf, sizeof(buf));

    TRY(close(fd));
    TRY(unlink(newpath));
    TRY(unlink("::dira"));
    TRY(unlink("::dirb"));
}

void test_link_errors(void) {
    printf("Test Link (errors)\n");

    const char* dirpath = "::dir";
    const char* oldpath = "::a";
    const char* newpath = "::b";
    const char* newpathdir = "::b/";

    // We should not be able to create hard links to directories
    TRY(mkdir(dirpath, 0755));
    EXPECT_FAIL(link(dirpath, newpath));
    TRY(unlink(dirpath));

    // We should not be able to create hard links to non-existent files
    EXPECT_FAIL(link(oldpath, newpath));
    assert(errno == ENOENT);

    int fd = TRY(open(oldpath, O_RDWR | O_CREAT | O_EXCL, 0644));
    TRY(close(fd));

    // We should not be able to link to or from . or ..
    EXPECT_FAIL(link(oldpath, "::."));
    EXPECT_FAIL(link(oldpath, "::.."));
    EXPECT_FAIL(link("::.", newpath));
    EXPECT_FAIL(link("::..", newpath));

    // We should not be able to link a file to itself
    EXPECT_FAIL(link(oldpath, oldpath));
    assert(errno == EEXIST);

    // We should not be able to link a file to a path that implies it must be a directory
    EXPECT_FAIL(link(oldpath, newpathdir));

    // After linking, we shouldn't be able to link again
    TRY(link(oldpath, newpath));
    EXPECT_FAIL(link(oldpath, newpath));
    assert(errno == EEXIST);
    // In either order
    EXPECT_FAIL(link(newpath, oldpath));
    assert(errno == EEXIST);

    TRY(unlink(newpath));
    TRY(unlink(oldpath));
}

int test_link(fs_info_t* info) {
    if (info->supports_hardlinks) {
        test_link_basic();
        test_link_between_dirs();
        test_link_errors();
        // TODO(smklein): Test that linking across filesystems fails
    } else {
        printf("Filesystem does not support hardlink\n");
    }
    return 0;
}
