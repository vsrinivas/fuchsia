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

typedef struct {
    // Buffer containing cached messages
    uint8_t buf[VFS_WATCH_MSG_MAX];
    // Offset into 'buf' of next message
    uint8_t* ptr;
    // Maximum size of buffer
    size_t size;
} watch_buffer_t;

// Try to read from the channel when it should be empty.
bool check_for_empty(watch_buffer_t* wb, mx_handle_t h) {
    char name[NAME_MAX + 1];
    ASSERT_NULL(wb->ptr);
    ASSERT_EQ(mx_channel_read(h, 0, &name, nullptr, sizeof(name), 0, nullptr, nullptr),
              MX_ERR_SHOULD_WAIT);
    return true;
}

bool check_local_event(watch_buffer_t* wb, const char* expected, uint8_t event) {
    size_t expected_len = strlen(expected);
    if (wb->ptr != nullptr) {
        // Used a cached event
        ASSERT_EQ(wb->ptr[0], event);
        ASSERT_EQ(wb->ptr[1], expected_len);
        ASSERT_EQ(memcmp(wb->ptr + 2, expected, expected_len), 0);
        wb->ptr = (uint8_t*)((uintptr_t)(wb->ptr) + expected_len + 2);
        ASSERT_LE((uintptr_t)wb->ptr, (uintptr_t) wb->buf + wb->size);
        if ((uintptr_t) wb->ptr == (uintptr_t) wb->buf + wb->size) {
            wb->ptr = nullptr;
        }
        return true;
    }
    return false;
}

// Try to read the 'expected' name off the channel.
bool check_for_event(watch_buffer_t* wb, mx_handle_t h, const char* expected, uint8_t event) {
    if (wb->ptr != nullptr) {
        return check_local_event(wb, expected, event);
    }

    mx_signals_t observed;
    ASSERT_EQ(mx_object_wait_one(h, MX_CHANNEL_READABLE,
                                 mx_deadline_after(MX_SEC(5)), &observed),
              MX_OK);
    ASSERT_EQ(observed & MX_CHANNEL_READABLE, MX_CHANNEL_READABLE);
    uint32_t actual;
    ASSERT_EQ(mx_channel_read(h, 0, wb->buf, nullptr, sizeof(wb->buf), 0,
                              &actual, nullptr), MX_OK);
    wb->size = actual;
    wb->ptr = wb->buf;
    return check_local_event(wb, expected, event);
}

bool test_watcher_add(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);
    mx_handle_t h;
    vfs_watch_dir_t request;
    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK);
    request.mask = VFS_WATCH_MASK_ADDED;
    request.options = 0;
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK);
    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));

    // The channel should be empty
    ASSERT_TRUE(check_for_empty(&wb, h));

    // Creating a file in the directory should trigger the watcher
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_TRUE(check_for_event(&wb, h, "foo", VFS_WATCH_EVT_ADDED));

    // Renaming into directory should trigger the watcher
    ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);
    ASSERT_TRUE(check_for_event(&wb, h, "bar", VFS_WATCH_EVT_ADDED));

    // Linking into directory should trigger the watcher
    ASSERT_EQ(link("::dir/bar", "::dir/blat"), 0);
    ASSERT_TRUE(check_for_event(&wb, h, "blat", VFS_WATCH_EVT_ADDED));

    // Clean up
    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_EQ(unlink("::dir/blat"), 0);

    // There shouldn't be anything else sitting around on the channel
    ASSERT_TRUE(check_for_empty(&wb, h));
    ASSERT_EQ(mx_handle_close(h), 0);

    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

bool test_watcher_existing(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);

    // Create a couple files in the directory
    int fd = open("::dir/foo", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    fd = open("::dir/bar", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

    // These files should be visible to the watcher through the "EXISTING"
    // mechanism.
    mx_handle_t h;
    vfs_watch_dir_t request;
    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK);
    request.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE;
    request.options = 0;
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK);
    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));

    // The channel should see the contents of the directory
    ASSERT_TRUE(check_for_event(&wb, h, ".", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, h, "foo", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, h, "bar", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb, h, "", VFS_WATCH_EVT_IDLE));
    ASSERT_TRUE(check_for_empty(&wb, h));

    // Now, if we choose to add additional files, they'll show up separately
    // with an "ADD" event.
    fd = open("::dir/baz", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_TRUE(check_for_event(&wb, h, "baz", VFS_WATCH_EVT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, h));

    // If we create a secondary watcher with the "EXISTING" request, we'll
    // see all files in the directory, but the first watcher won't see anything.
    mx_handle_t h2;
    ASSERT_EQ(mx_channel_create(0, &h2, &request.channel), MX_OK);
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK);
    watch_buffer_t wb2;
    memset(&wb2, 0, sizeof(wb2));
    ASSERT_TRUE(check_for_event(&wb2, h2, ".", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, h2, "foo", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, h2, "bar", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, h2, "baz", VFS_WATCH_EVT_EXISTING));
    ASSERT_TRUE(check_for_event(&wb2, h2, "", VFS_WATCH_EVT_IDLE));
    ASSERT_TRUE(check_for_empty(&wb2, h2));
    ASSERT_TRUE(check_for_empty(&wb, h));

    // Clean up
    ASSERT_EQ(unlink("::dir/foo"), 0);
    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_EQ(unlink("::dir/baz"), 0);

    // There shouldn't be anything else sitting around on either channel
    ASSERT_TRUE(check_for_empty(&wb, h));
    ASSERT_EQ(mx_handle_close(h), 0);
    ASSERT_TRUE(check_for_empty(&wb2, h2));
    ASSERT_EQ(mx_handle_close(h2), 0);

    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

bool test_watcher_removed(void) {
    BEGIN_TEST;

    if (!test_info->supports_watchers) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0666), 0);
    DIR* dir = opendir("::dir");
    ASSERT_NONNULL(dir);
    mx_handle_t h;
    vfs_watch_dir_t request;

    ASSERT_EQ(mx_channel_create(0, &h, &request.channel), MX_OK);
    request.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_REMOVED;
    request.options = 0;

    watch_buffer_t wb;
    memset(&wb, 0, sizeof(wb));
    ASSERT_EQ(ioctl_vfs_watch_dir(dirfd(dir), &request), MX_OK);

    ASSERT_TRUE(check_for_empty(&wb, h));

    int fd = openat(dirfd(dir), "foo", O_CREAT | O_RDWR | O_EXCL);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);

    ASSERT_TRUE(check_for_event(&wb, h, "foo", VFS_WATCH_EVT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, h));

    ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);

    ASSERT_TRUE(check_for_event(&wb, h, "foo", VFS_WATCH_EVT_REMOVED));
    ASSERT_TRUE(check_for_event(&wb, h, "bar", VFS_WATCH_EVT_ADDED));
    ASSERT_TRUE(check_for_empty(&wb, h));

    ASSERT_EQ(unlink("::dir/bar"), 0);
    ASSERT_TRUE(check_for_event(&wb, h, "bar", VFS_WATCH_EVT_REMOVED));
    ASSERT_TRUE(check_for_empty(&wb, h));

    mx_handle_close(h);
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(directory_watcher_tests,
    RUN_TEST_MEDIUM(test_watcher_add)
    RUN_TEST_MEDIUM(test_watcher_existing)
    RUN_TEST_MEDIUM(test_watcher_removed)
)
