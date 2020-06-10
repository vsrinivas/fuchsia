// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>

#include "filesystems.h"
#include "misc.h"

namespace fio = ::llcpp::fuchsia::io;

typedef struct {
  // Buffer containing cached messages
  uint8_t buf[fio::MAX_BUF];
  // Offset into 'buf' of next message
  uint8_t* ptr;
  // Maximum size of buffer
  size_t size;
} watch_buffer_t;

// Try to read from the channel when it should be empty.
bool check_for_empty(watch_buffer_t* wb, const zx::channel& c) {
  char name[NAME_MAX + 1];
  ASSERT_NULL(wb->ptr);
  ASSERT_EQ(c.read(0, &name, nullptr, sizeof(name), 0, nullptr, nullptr), ZX_ERR_SHOULD_WAIT);
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
    ASSERT_LE((uintptr_t)wb->ptr, (uintptr_t)wb->buf + wb->size);
    if ((uintptr_t)wb->ptr == (uintptr_t)wb->buf + wb->size) {
      wb->ptr = nullptr;
    }
    return true;
  }
  return false;
}

// Try to read the 'expected' name off the channel.
bool check_for_event(watch_buffer_t* wb, const zx::channel& c, const char* expected,
                     uint8_t event) {
  if (wb->ptr != nullptr) {
    return check_local_event(wb, expected, event);
  }

  zx_signals_t observed;
  ASSERT_EQ(c.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(5)), &observed), ZX_OK);
  ASSERT_EQ(observed & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
  uint32_t actual;
  ASSERT_EQ(c.read(0, wb->buf, nullptr, sizeof(wb->buf), 0, &actual, nullptr), ZX_OK);
  wb->size = actual;
  wb->ptr = wb->buf;
  return check_local_event(wb, expected, event);
}

bool TestWatcherAdd(void) {
  BEGIN_TEST;

  if (!test_info->supports_watchers) {
    return true;
  }

  ASSERT_EQ(mkdir("::dir", 0666), 0);
  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);
  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));

  auto watch_result =
      fio::Directory::Call::Watch(caller.channel(), fio::WATCH_MASK_ADDED, 0, std::move(server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result.Unwrap()->s, ZX_OK);

  watch_buffer_t wb;
  memset(&wb, 0, sizeof(wb));

  // The channel should be empty
  ASSERT_TRUE(check_for_empty(&wb, client));

  // Creating a file in the directory should trigger the watcher
  fbl::unique_fd fd(open("::dir/foo", O_RDWR | O_CREAT));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_TRUE(check_for_event(&wb, client, "foo", fio::WATCH_EVENT_ADDED));

  // Renaming into directory should trigger the watcher
  ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);
  ASSERT_TRUE(check_for_event(&wb, client, "bar", fio::WATCH_EVENT_ADDED));

  // Linking into directory should trigger the watcher
  ASSERT_EQ(link("::dir/bar", "::dir/blat"), 0);
  ASSERT_TRUE(check_for_event(&wb, client, "blat", fio::WATCH_EVENT_ADDED));

  // Clean up
  ASSERT_EQ(unlink("::dir/bar"), 0);
  ASSERT_EQ(unlink("::dir/blat"), 0);

  // There shouldn't be anything else sitting around on the channel
  ASSERT_TRUE(check_for_empty(&wb, client));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir("::dir"), 0);

  END_TEST;
}

bool TestWatcherExisting(void) {
  BEGIN_TEST;

  if (!test_info->supports_watchers) {
    return true;
  }

  ASSERT_EQ(mkdir("::dir", 0666), 0);
  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);

  // Create a couple files in the directory
  fbl::unique_fd fd(open("::dir/foo", O_RDWR | O_CREAT));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  fd.reset(open("::dir/bar", O_RDWR | O_CREAT));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  // These files should be visible to the watcher through the "EXISTING"
  // mechanism.
  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
  uint32_t mask = fio::WATCH_MASK_ADDED | fio::WATCH_MASK_EXISTING | fio::WATCH_MASK_IDLE;
  auto watch_result = fio::Directory::Call::Watch(caller.channel(), mask, 0, std::move(server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result.Unwrap()->s, ZX_OK);
  watch_buffer_t wb;
  memset(&wb, 0, sizeof(wb));

  // The channel should see the contents of the directory
  ASSERT_TRUE(check_for_event(&wb, client, ".", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb, client, "foo", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb, client, "bar", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb, client, "", fio::WATCH_EVENT_IDLE));
  ASSERT_TRUE(check_for_empty(&wb, client));

  // Now, if we choose to add additional files, they'll show up separately
  // with an "ADD" event.
  fd.reset(open("::dir/baz", O_RDWR | O_CREAT));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_TRUE(check_for_event(&wb, client, "baz", fio::WATCH_EVENT_ADDED));
  ASSERT_TRUE(check_for_empty(&wb, client));

  // If we create a secondary watcher with the "EXISTING" request, we'll
  // see all files in the directory, but the first watcher won't see anything.
  zx::channel client2;
  ASSERT_EQ(zx::channel::create(0, &client2, &server), ZX_OK);
  watch_result = fio::Directory::Call::Watch(caller.channel(), mask, 0, std::move(server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result.Unwrap()->s, ZX_OK);
  watch_buffer_t wb2;
  memset(&wb2, 0, sizeof(wb2));
  ASSERT_TRUE(check_for_event(&wb2, client2, ".", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb2, client2, "foo", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb2, client2, "bar", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb2, client2, "baz", fio::WATCH_EVENT_EXISTING));
  ASSERT_TRUE(check_for_event(&wb2, client2, "", fio::WATCH_EVENT_IDLE));
  ASSERT_TRUE(check_for_empty(&wb2, client2));
  ASSERT_TRUE(check_for_empty(&wb, client));

  // Clean up
  ASSERT_EQ(unlink("::dir/foo"), 0);
  ASSERT_EQ(unlink("::dir/bar"), 0);
  ASSERT_EQ(unlink("::dir/baz"), 0);

  // There shouldn't be anything else sitting around on either channel
  ASSERT_TRUE(check_for_empty(&wb, client));
  ASSERT_TRUE(check_for_empty(&wb2, client2));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir("::dir"), 0);

  END_TEST;
}

bool TestWatcherRemoved(void) {
  BEGIN_TEST;

  if (!test_info->supports_watchers) {
    return true;
  }

  ASSERT_EQ(mkdir("::dir", 0666), 0);
  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);

  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  uint32_t mask = fio::WATCH_MASK_ADDED | fio::WATCH_MASK_REMOVED;
  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));

  auto watch_result = fio::Directory::Call::Watch(caller.channel(), mask, 0, std::move(server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result.Unwrap()->s, ZX_OK);

  watch_buffer_t wb;
  memset(&wb, 0, sizeof(wb));

  ASSERT_TRUE(check_for_empty(&wb, client));

  fbl::unique_fd fd(openat(dirfd(dir), "foo", O_CREAT | O_RDWR | O_EXCL));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_TRUE(check_for_event(&wb, client, "foo", fio::WATCH_EVENT_ADDED));
  ASSERT_TRUE(check_for_empty(&wb, client));

  ASSERT_EQ(rename("::dir/foo", "::dir/bar"), 0);

  ASSERT_TRUE(check_for_event(&wb, client, "foo", fio::WATCH_EVENT_REMOVED));
  ASSERT_TRUE(check_for_event(&wb, client, "bar", fio::WATCH_EVENT_ADDED));
  ASSERT_TRUE(check_for_empty(&wb, client));

  ASSERT_EQ(unlink("::dir/bar"), 0);
  ASSERT_TRUE(check_for_event(&wb, client, "bar", fio::WATCH_EVENT_REMOVED));
  ASSERT_TRUE(check_for_empty(&wb, client));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir("::dir"), 0);

  END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(directory_watcher_tests,
                        RUN_TEST_MEDIUM(TestWatcherAdd) RUN_TEST_MEDIUM(TestWatcherExisting)
                            RUN_TEST_MEDIUM(TestWatcherRemoved))
