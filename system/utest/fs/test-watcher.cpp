// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
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

#include <magenta/device/vfs.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>

#include "filesystems.h"
#include "misc.h"

// Try to read from the channel when it should be empty.
bool check_for_empty(mx_handle_t h) {
    char name[NAME_MAX + 1];
    ASSERT_EQ(mx_channel_read(h, 0, &name, NULL, sizeof(name), 0, NULL, NULL),
              MX_ERR_SHOULD_WAIT, "");
    return true;
}

// Try to read the 'expected' name off the channel.
bool check_for_event(mx_handle_t h, const char* expected, uint8_t event) {
    mx_signals_t observed;
    ASSERT_EQ(mx_object_wait_one(h, MX_CHANNEL_READABLE,
                                 mx_deadline_after(MX_SEC(5)), &observed),
              MX_OK, "");
    ASSERT_EQ(observed & MX_CHANNEL_READABLE, MX_CHANNEL_READABLE, "");
    uint32_t actual;

    size_t expected_len = strlen(expected);
    uint8_t msg[expected_len + 2];
    ASSERT_EQ(mx_channel_read(h, 0, msg, NULL,
                              static_cast<uint32_t>(expected_len + 2), 0,
                              &actual, NULL), MX_OK, "");
    ASSERT_EQ(actual, expected_len + 2, "");
    ASSERT_EQ(msg[0], event, "");
    ASSERT_EQ(msg[1], expected_len, "");
    ASSERT_EQ(memcmp(msg + 2, expected, expected_len), 0, "");
    return true;
}

bool test_watcher_add(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0, "");
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir, "");
    mx_handle_t h;
    vfs_watch_dir_t request;
    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK, "");
    request.mask = VFS_WATCH_MASK_ADDED;
    request.options = 0;
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK, "");

    // The channel should be empty
    ASSERT_TRUE(check_for_empty(h), "");

    // Creating a file in the directory should trigger the watcher
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_TRUE(check_for_event(h, "foo", VFS_WATCH_EVT_ADDED), "");

    // Renaming into directory should trigger the watcher
    ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0, "");
    ASSERT_TRUE(check_for_event(h, "bar", VFS_WATCH_EVT_ADDED), "");

    // Linking into directory should trigger the watcher
    ASSERT_EQ(link("::dir/bar", "::dir/blat"), 0, "");
    ASSERT_TRUE(check_for_event(h, "blat", VFS_WATCH_EVT_ADDED), "");

    // Clean up
    ASSERT_EQ(unlink("::dir/bar"), 0, "");
    ASSERT_EQ(unlink("::dir/blat"), 0, "");

    // There shouldn't be anything else sitting around on the channel
    ASSERT_TRUE(check_for_empty(h), "");
    ASSERT_EQ(mx_handle_close(h), 0, "");

    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(rmdir("::dir"), 0, "");

    END_TEST;
}

bool test_watcher_existing(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0, "");
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir, "");

    // Create a couple files in the directory
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    fd = open("::dir/bar", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");

    // These files should be visible to the watcher through the "EXISTING"
    // mechanism.
    mx_handle_t h;
    vfs_watch_dir_t request;
    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK, "");
    request.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE;
    request.options = 0;
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK, "");

    // The channel should see the contents of the directory
    ASSERT_TRUE(check_for_event(h, ".", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h, "foo", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h, "bar", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h, "", VFS_WATCH_EVT_IDLE), "");
    ASSERT_TRUE(check_for_empty(h), "");

    // Now, if we choose to add additional files, they'll show up separately
    // with an "ADD" event.
    fd = open("::dir/baz", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_TRUE(check_for_event(h, "baz", VFS_WATCH_EVT_ADDED), "");
    ASSERT_TRUE(check_for_empty(h), "");

    // If we create a secondary watcher with the "EXISTING" request, we'll
    // see all files in the directory, but the first watcher won't see anything.
    mx_handle_t h2;
    ASSERT_EQ(mx_channel_create(0, &h2, &request.channel), MX_OK, "");
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK, "");
    ASSERT_TRUE(check_for_event(h2, ".", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h2, "foo", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h2, "bar", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h2, "baz", VFS_WATCH_EVT_EXISTING), "");
    ASSERT_TRUE(check_for_event(h2, "", VFS_WATCH_EVT_IDLE), "");
    ASSERT_TRUE(check_for_empty(h2), "");
    ASSERT_TRUE(check_for_empty(h), "");

    // Clean up
    ASSERT_EQ(unlink("::dir/foo"), 0, "");
    ASSERT_EQ(unlink("::dir/bar"), 0, "");
    ASSERT_EQ(unlink("::dir/baz"), 0, "");

    // There shouldn't be anything else sitting around on either channel
    ASSERT_TRUE(check_for_empty(h), "");
    ASSERT_EQ(mx_handle_close(h), 0, "");
    ASSERT_TRUE(check_for_empty(h2), "");
    ASSERT_EQ(mx_handle_close(h2), 0, "");

    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(rmdir("::dir"), 0, "");

    END_TEST;
}

bool test_watcher_unsupported(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0, "");
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir, "");
    mx_handle_t h;
    vfs_watch_dir_t request;

    // Ask to watch an unsupported event
    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK, "");
    request.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_DELETED;
    request.options = 0;
    ASSERT_NEQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK, "");
    mx_handle_close(h);

    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(rmdir("::dir"), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(directory_watcher_tests,
    RUN_TEST_MEDIUM(test_watcher_add)
    RUN_TEST_MEDIUM(test_watcher_existing)
    RUN_TEST_MEDIUM(test_watcher_unsupported)
)
