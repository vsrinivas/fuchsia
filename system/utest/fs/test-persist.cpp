// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>

#include "filesystems.h"
#include "misc.h"

constexpr bool is_directory(const char* const path) {
    return path[fbl::constexpr_strlen(path) - 1] == '/';
}

bool test_persist_simple(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    const char* const paths[] = {
        "::abc",
        "::def/",
        "::def/def_subdir/",
        "::def/def_subdir/def_subfile",
        "::ghi",
        "::jkl",
        "::mnopqrstuvxyz"
    };
    for (size_t i = 0; i < fbl::count_of(paths); i++) {
        if (is_directory(paths[i])) {
            ASSERT_EQ(mkdir(paths[i], 0644), 0);
        } else {
            int fd = open(paths[i], O_RDWR | O_CREAT | O_EXCL, 0644);
            ASSERT_GT(fd, 0);
            ASSERT_EQ(close(fd), 0);
        }
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // The files should still exist when we remount
    for (ssize_t i = fbl::count_of(paths) - 1; i >= 0; i--) {
        if (is_directory(paths[i])) {
            ASSERT_EQ(rmdir(paths[i]), 0);
        } else {
            ASSERT_EQ(unlink(paths[i]), 0);
        }
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // But they should stay deleted!
    for (ssize_t i = fbl::count_of(paths) - 1; i >= 0; i--) {
        if (is_directory(paths[i])) {
            ASSERT_EQ(rmdir(paths[i]), -1);
        } else {
            ASSERT_EQ(unlink(paths[i]), -1);
        }
    }

    END_TEST;
}

bool test_persist_rapid_remount(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    for (size_t i = 0; i < 100; i++) {
        ASSERT_TRUE(check_remount(), "Could not remount filesystem");
    }

    END_TEST;
}


template <size_t BufferSize>
bool test_persist_with_data(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    const char* const files[] = {
        "::abc",
        "::def",
        "::and-another-file-filled-with-data",
    };
    fbl::unique_ptr<uint8_t[]> buffers[fbl::count_of(files)];
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    unittest_printf("Persistent data test using seed: %u\n", seed);
    fbl::AllocChecker ac;
    for (size_t i = 0; i < fbl::count_of(files); i++) {
        buffers[i].reset(new (&ac) uint8_t[BufferSize]);
        ASSERT_TRUE(ac.check());

        for (size_t j = 0; j < BufferSize; j++) {
            buffers[i][j] = (uint8_t) rand_r(&seed);
        }
        int fd = open(files[i], O_RDWR | O_CREAT, 0644);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(write(fd, &buffers[i][0], BufferSize), BufferSize);
        ASSERT_EQ(fsync(fd), 0);
        ASSERT_EQ(close(fd), 0);
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // Read files
    for (size_t i = 0; i < fbl::count_of(files); i++) {
        fbl::unique_ptr<uint8_t[]> rbuf(new (&ac) uint8_t[BufferSize]);
        ASSERT_TRUE(ac.check());
        int fd = open(files[i], O_RDONLY, 0644);
        ASSERT_GT(fd, 0);

        struct stat buf;
        ASSERT_EQ(fstat(fd, &buf), 0);
        ASSERT_EQ(buf.st_nlink, 1);
        ASSERT_EQ(buf.st_size, BufferSize);

        ASSERT_EQ(read(fd, &rbuf[0], BufferSize), BufferSize);
        for (size_t j = 0; j < BufferSize; j++) {
            ASSERT_EQ(rbuf[j], buffers[i][j]);
        }

        ASSERT_EQ(close(fd), 0);
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // Delete all files
    for (size_t i = 0; i < fbl::count_of(files); i++) {
        ASSERT_EQ(unlink(files[i]), 0);
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // Files should stay deleted

    DIR* dirp = opendir("::.");
    ASSERT_NONNULL(dirp);
    struct dirent* de;
    de = readdir(dirp);
    ASSERT_NONNULL(de);
    ASSERT_EQ(strncmp(de->d_name, ".", 1), 0);
    ASSERT_NULL(readdir(dirp));
    ASSERT_EQ(closedir(dirp), 0);

    END_TEST;
}

constexpr size_t kMaxLoopLength = 26;

template <bool MoveDirectory, size_t LoopLength, size_t Moves>
bool test_rename_loop(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
        return true;
    }

    static_assert(LoopLength <= kMaxLoopLength, "Loop length too long");

    char src[128];
    // Create "LoopLength" directories
    for (size_t i = 0; i < LoopLength; i++) {
        ASSERT_GT(sprintf(src, "::%c", static_cast<char>('a' + i)), 0);
        ASSERT_EQ(mkdir(src, 0644), 0);
    }

    // Create a 'target'
    if (MoveDirectory) {
        ASSERT_EQ(mkdir("::a/target", 0644), 0);
    } else {
        int fd = open("::a/target", O_RDWR | O_CREAT);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(close(fd), 0);
    }

    // Move the target through the loop a bunch of times
    size_t moves = Moves;
    strcpy(src, "::a/target");
    size_t char_index = 0;
    while (moves--) {
        char dst[128];
        strcpy(dst, src);
        char_index = (char_index + 1) % LoopLength;
        dst[2] = static_cast<char>('a' + char_index);
        ASSERT_EQ(rename(src, dst), 0);
        strcpy(src, dst);
    }

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // Check that the target only exists in ONE directory
    bool target_found = false;
    for (size_t i = 0; i < LoopLength; i++) {
        ASSERT_GT(sprintf(src, "::%c", static_cast<char>('a' + i)), 0);
        DIR* dirp = opendir(src);
        ASSERT_NONNULL(dirp);
        struct dirent* de;
        de = readdir(dirp);
        ASSERT_NONNULL(de);
        ASSERT_EQ(strcmp(de->d_name, "."), 0);
        de = readdir(dirp);
        if (de != nullptr) {
            ASSERT_FALSE(target_found, "Target found twice!");
            ASSERT_EQ(strcmp(de->d_name, "target"), 0, "Non-target found");
            target_found = true;
        }

        ASSERT_EQ(closedir(dirp), 0);
    }
    ASSERT_TRUE(target_found);

    ASSERT_TRUE(check_remount(), "Could not remount filesystem");

    // Clean up

    target_found = false;
    for (size_t i = 0; i < LoopLength; i++) {
        ASSERT_GT(sprintf(src, "::%c", static_cast<char>('a' + i)), 0);
        int ret = unlink(src);
        if (ret != 0) {
            ASSERT_FALSE(target_found);
            ASSERT_GT(sprintf(src, "::%c/target", static_cast<char>('a' + i)), 0);
            ASSERT_EQ(unlink(src), 0);
            ASSERT_GT(sprintf(src, "::%c", static_cast<char>('a' + i)), 0);
            ASSERT_EQ(unlink(src), 0);
            target_found = true;
        }
    }
    ASSERT_TRUE(target_found, "Target was never unlinked");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(persistence_tests,
    RUN_TEST_MEDIUM(test_persist_simple)
    RUN_TEST_LARGE(test_persist_rapid_remount)
    RUN_TEST_MEDIUM((test_persist_with_data<1>))
    RUN_TEST_MEDIUM((test_persist_with_data<100>))
    RUN_TEST_MEDIUM((test_persist_with_data<8192 - 1>))
    RUN_TEST_MEDIUM((test_persist_with_data<8192>))
    RUN_TEST_MEDIUM((test_persist_with_data<8192 + 1>))
    RUN_TEST_LARGE((test_persist_with_data<8192 * 128>))
    RUN_TEST_MEDIUM((test_rename_loop<false, 2, 2>));
    RUN_TEST_MEDIUM((test_rename_loop<false, 2, 100>));
    RUN_TEST_LARGE((test_rename_loop<false, 15, 100>));
    RUN_TEST_LARGE((test_rename_loop<false, 25, 500>));
    RUN_TEST_MEDIUM((test_rename_loop<true, 2, 2>));
    RUN_TEST_MEDIUM((test_rename_loop<true, 2, 100>));
    RUN_TEST_MEDIUM((test_rename_loop<true, 15, 100>));
    RUN_TEST_LARGE((test_rename_loop<true, 25, 500>));
)
