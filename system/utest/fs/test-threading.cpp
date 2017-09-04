// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <fbl/atomic.h>
#include <unittest/unittest.h>

#include "filesystems.h"
#include "misc.h"

// Try repeatedly creating and removing a file within a directory,
// as fast as possible, in an attempt to trigger filesystem-internal
// threading races between creation and deletion of a file.
template <bool reuse_subdirectory>
bool test_inode_reuse(void) {
    BEGIN_TEST;

    ASSERT_EQ(mkdir("::reuse", 0755), 0, "");
    DIR* d = opendir("::reuse");
    ASSERT_NONNULL(d, "");
    for (size_t i = 0; i < 1000; i++) {
        ASSERT_EQ(mkdirat(dirfd(d), "foo", 0666), 0, "");
        if (reuse_subdirectory) {
            ASSERT_EQ(mkdirat(dirfd(d), "foo/bar", 0666), 0, "");
            ASSERT_EQ(unlinkat(dirfd(d), "foo/bar", 0), 0, "");
        }
        ASSERT_EQ(unlinkat(dirfd(d), "foo", 0), 0, "");
    }
    ASSERT_EQ(closedir(d), 0, "");
    ASSERT_EQ(rmdir("::reuse"), 0, "");
    END_TEST;
}

// Return codes from helper threads
constexpr int kSuccess = 1;
constexpr int kFailure = -1;
constexpr int kUnexpectedFailure = -2;

using thrd_cb_t = int(void*);

// Launch some threads, and have them all execute callback
// 'cb'.
//
// It is expected that:
//  - kSuccessCount threads will return "kSuccess"
//  - ALL OTHER threads will return "kFailure"
//
// In any other condition, this helper fails.
// For example, returning "kUnexpectedFailure" from cb
// is an easy way to fail the entire test from a background thread.
template <size_t kNumThreads, size_t kSuccessCount>
bool thread_action_test(thrd_cb_t cb, void* arg = nullptr) {
    BEGIN_HELPER;

    static_assert(kNumThreads >= kSuccessCount, "Need more threads or less successes");

    thrd_t threads[kNumThreads];
    for (size_t i = 0; i < kNumThreads; i++) {
        ASSERT_EQ(thrd_create(&threads[i], cb, arg), thrd_success);
    }

    size_t success_count = 0;
    for (size_t i = 0; i < kNumThreads; i++) {
        int rc;
        ASSERT_EQ(thrd_join(threads[i], &rc), thrd_success);
        if (rc == kSuccess) {
            success_count++;
            ASSERT_LE(success_count, kSuccessCount, "Too many succeeding threads");
        } else {
            ASSERT_EQ(rc, kFailure, "Unexpected return code from worker thread");
        }
    }
    ASSERT_EQ(success_count, kSuccessCount, "Not enough succeeding threads");

    END_HELPER;
}

constexpr size_t kIterCount = 10;

bool test_create_unlink_exclusive(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kIterCount; i++) {
        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            int fd = open("::exclusive", O_RDWR | O_CREAT | O_EXCL);
            if (fd > 0) {
                return close(fd) == 0 ? kSuccess : kUnexpectedFailure;
            } else if (errno == EEXIST) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));

        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            if (unlink("::exclusive") == 0) {
                return kSuccess;
            } else if (errno == ENOENT) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));
    }
    END_TEST;
}

bool test_mkdir_rmdir_exclusive(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kIterCount; i++) {
        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            if (mkdir("::exclusive", 0666) == 0) {
                return kSuccess;
            } else if (errno == EEXIST) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));

        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            if (rmdir("::exclusive") == 0) {
                return kSuccess;
            } else if (errno == ENOENT) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));
    }
    END_TEST;
}

bool test_rename_exclusive(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kIterCount; i++) {

        // Test case of renaming from a single source.
        ASSERT_EQ(mkdir("::rename_start", 0666), 0);
        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            if (rename("::rename_start", "::rename_end") == 0) {
                return kSuccess;
            } else if (errno == ENOENT) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));
        ASSERT_EQ(rmdir("::rename_end"), 0);

        // Test case of renaming from multiple sources at once,
        // to a single destination
        fbl::atomic<uint32_t> ctr{0};
        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            auto ctr = reinterpret_cast<fbl::atomic<uint32_t>*>(arg);
            char start[128];
            snprintf(start, sizeof(start) - 1, "::rename_start_%u", ctr->fetch_add(1));
            if (mkdir(start, 0666)) {
                return kUnexpectedFailure;
            }

            // Give the target a child, so it cannot be overwritten as a target
            char child[256];
            snprintf(child, sizeof(child) - 1, "%s/child", start);
            if (mkdir(child, 0666)) {
                return kUnexpectedFailure;
            }

            if (rename(start, "::rename_end") == 0) {
                return kSuccess;
            } else if (errno == ENOTEMPTY || errno == EEXIST) {
                return rmdir(child) == 0 && rmdir(start) == 0 ? kFailure :
                        kUnexpectedFailure;
            }
            return kUnexpectedFailure;
        }, &ctr)));

        DIR* dir = opendir("::rename_end");
        ASSERT_NONNULL(dir);
        struct dirent* de;
        while ((de = readdir(dir)) && de != nullptr) {
            unlinkat(dirfd(dir), de->d_name, AT_REMOVEDIR);
        }
        ASSERT_EQ(closedir(dir), 0);
        ASSERT_EQ(rmdir("::rename_end"), 0);
    }
    END_TEST;
}

bool test_rename_overwrite(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kIterCount; i++) {
        // Test case of renaming from multiple sources at once,
        // to a single destination
        fbl::atomic<uint32_t> ctr{0};
        ASSERT_TRUE((thread_action_test<10, 10>([](void* arg) {
            auto ctr = reinterpret_cast<fbl::atomic<uint32_t>*>(arg);
            char start[128];
            snprintf(start, sizeof(start) - 1, "::rename_start_%u", ctr->fetch_add(1));
            if (mkdir(start, 0666)) {
                return kUnexpectedFailure;
            }
            if (rename(start, "::rename_end") == 0) {
                return kSuccess;
            }
            return kUnexpectedFailure;
        }, &ctr)));
        ASSERT_EQ(rmdir("::rename_end"), 0);
    }
    END_TEST;
}

bool test_link_exclusive(void) {
    BEGIN_TEST;

    if (!test_info->supports_hardlinks) {
        return true;
    }

    for (size_t i = 0; i < kIterCount; i++) {
        int fd = open("::link_start", O_RDWR | O_CREAT | O_EXCL);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(close(fd), 0);

        ASSERT_TRUE((thread_action_test<10, 1>([](void* arg) {
            if (link("::link_start", "::link_end") == 0) {
                return kSuccess;
            } else if (errno == EEXIST) {
                return kFailure;
            }
            return kUnexpectedFailure;
        })));

        ASSERT_EQ(unlink("::link_start"), 0);
        ASSERT_EQ(unlink("::link_end"), 0);
        ASSERT_TRUE(check_remount());
    }
    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(threading_tests,
    RUN_TEST_MEDIUM((test_inode_reuse<false>))
    RUN_TEST_MEDIUM((test_inode_reuse<true>))
    RUN_TEST_MEDIUM(test_create_unlink_exclusive)
    RUN_TEST_MEDIUM(test_mkdir_rmdir_exclusive)
    RUN_TEST_MEDIUM(test_rename_exclusive)
    RUN_TEST_MEDIUM(test_rename_overwrite)
    RUN_TEST_MEDIUM(test_link_exclusive)
)
