// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

namespace fio = fuchsia_io;

using WatcherTest = FilesystemTest;

struct WatchBuffer {
  // Buffer containing cached messages
  uint8_t buf[fio::wire::kMaxBuf];
  // Offset into 'buf' of next message
  uint8_t* ptr;
  // Maximum size of buffer
  size_t size;
};

// Try to read from the channel when it should be empty.
void CheckForEmpty(WatchBuffer* wb, const fidl::ClientEnd<fio::DirectoryWatcher>& c) {
  char name[NAME_MAX + 1];
  ASSERT_EQ(wb->ptr, nullptr);
  ASSERT_EQ(c.channel().read(0, &name, nullptr, sizeof(name), 0, nullptr, nullptr),
            ZX_ERR_SHOULD_WAIT);
}

void CheckLocalEvent(WatchBuffer* wb, const char* expected, fio::wire::WatchEvent event) {
  size_t expected_len = strlen(expected);
  ASSERT_NE(wb->ptr, nullptr);
  // Used a cached event
  ASSERT_EQ(static_cast<fio::wire::WatchEvent>(wb->ptr[0]), event);
  ASSERT_EQ(wb->ptr[1], expected_len);
  ASSERT_EQ(memcmp(wb->ptr + 2, expected, expected_len), 0);
  wb->ptr += expected_len + 2;
  ASSERT_LE(wb->ptr, wb->buf + wb->size);
  if (wb->ptr == wb->buf + wb->size) {
    wb->ptr = nullptr;
  }
}

// Try to read the 'expected' name off the channel.
void CheckForEvent(WatchBuffer* wb, const fidl::ClientEnd<fio::DirectoryWatcher>& c,
                   const char* expected, fio::wire::WatchEvent event) {
  if (wb->ptr == nullptr) {
    zx_signals_t observed;
    ASSERT_EQ(c.channel().wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(5)), &observed),
              ZX_OK);
    ASSERT_EQ(observed & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
    uint32_t actual;
    ASSERT_EQ(c.channel().read(0, wb->buf, nullptr, sizeof(wb->buf), 0, &actual, nullptr), ZX_OK);
    wb->size = actual;
    wb->ptr = wb->buf;
  }
  CheckLocalEvent(wb, expected, event);
}

TEST_P(WatcherTest, Add) {
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0666), 0);
  DIR* dir = opendir(GetPath("dir").c_str());
  ASSERT_NE(dir, nullptr);

  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));

  auto watch_result = fidl::WireCall(caller.borrow_as<fio::Directory>())
                          ->Watch(fio::wire::WatchMask::kAdded, 0, std::move(endpoints->server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result->s, ZX_OK);

  WatchBuffer wb;
  memset(&wb, 0, sizeof(wb));

  // The channel should be empty
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // Creating a file in the directory should trigger the watcher
  fbl::unique_fd fd(open(GetPath("dir/foo").c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "foo", fio::wire::WatchEvent::kAdded));

  // Renaming into directory should trigger the watcher
  ASSERT_EQ(rename(GetPath("dir/foo").c_str(), GetPath("dir/bar").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "bar", fio::wire::WatchEvent::kAdded));

  if (fs().GetTraits().supports_hard_links) {
    // Linking into directory should trigger the watcher
    ASSERT_EQ(link(GetPath("dir/bar").c_str(), GetPath("dir/blat").c_str()), 0);
    ASSERT_NO_FATAL_FAILURE(
        CheckForEvent(&wb, endpoints->client, "blat", fio::wire::WatchEvent::kAdded));
    ASSERT_EQ(unlink(GetPath("dir/blat").c_str()), 0);
  }

  // Clean up
  ASSERT_EQ(unlink(GetPath("dir/bar").c_str()), 0) << errno;

  // There shouldn't be anything else sitting around on the channel
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir(GetPath("dir").c_str()), 0) << errno;
}

TEST_P(WatcherTest, Existing) {
  // This test currently makes assumptions about the order in which entries are returned.  For now,
  // it creates entries in alphabetical order, which happens to work on filesystems we currently
  // support.
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0666), 0);
  DIR* dir = opendir(GetPath("dir").c_str());
  ASSERT_NE(dir, nullptr);

  // Create a couple files in the directory
  fbl::unique_fd fd(open(GetPath("dir/bar").c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  fd.reset(open(GetPath("dir/foo").c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  // These files should be visible to the watcher through the "EXISTING"
  // mechanism.
  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
  fio::wire::WatchMask mask =
      fio::wire::WatchMask::kAdded | fio::wire::WatchMask::kExisting | fio::wire::WatchMask::kIdle;
  {
    auto watch_result = fidl::WireCall(caller.borrow_as<fio::Directory>())
                            ->Watch(mask, 0, std::move(endpoints->server));
    ASSERT_EQ(watch_result.status(), ZX_OK);
    ASSERT_EQ(watch_result->s, ZX_OK);
  }
  WatchBuffer wb;
  memset(&wb, 0, sizeof(wb));

  // The channel should see the contents of the directory
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, ".", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "bar", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "foo", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(CheckForEvent(&wb, endpoints->client, "", fio::wire::WatchEvent::kIdle));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // Now, if we choose to add additional files, they'll show up separately
  // with an "ADD" event.
  fd.reset(open(GetPath("dir/goo").c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "goo", fio::wire::WatchEvent::kAdded));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // If we create a secondary watcher with the "EXISTING" request, we'll
  // see all files in the directory, but the first watcher won't see anything.
  zx::result endpoints2 = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  ASSERT_TRUE(endpoints2.is_ok()) << endpoints.status_string();
  {
    auto watch_result = fidl::WireCall(caller.borrow_as<fio::Directory>())
                            ->Watch(mask, 0, std::move(endpoints2->server));
    ASSERT_EQ(watch_result.status(), ZX_OK);
    ASSERT_EQ(watch_result->s, ZX_OK);
  }
  WatchBuffer wb2;
  memset(&wb2, 0, sizeof(wb2));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb2, endpoints2->client, ".", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb2, endpoints2->client, "bar", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb2, endpoints2->client, "foo", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb2, endpoints2->client, "goo", fio::wire::WatchEvent::kExisting));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb2, endpoints2->client, "", fio::wire::WatchEvent::kIdle));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb2, endpoints2->client));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // Clean up
  ASSERT_EQ(unlink(GetPath("dir/bar").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir/foo").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("dir/goo").c_str()), 0);

  // There shouldn't be anything else sitting around on either channel
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb2, endpoints2->client));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir(GetPath("dir").c_str()), 0);
}

TEST_P(WatcherTest, Removed) {
  ASSERT_EQ(mkdir(GetPath("dir").c_str(), 0666), 0);
  DIR* dir = opendir(GetPath("dir").c_str());
  ASSERT_NE(dir, nullptr);

  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fio::wire::WatchMask mask = fio::wire::WatchMask::kAdded | fio::wire::WatchMask::kRemoved;
  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));

  auto watch_result = fidl::WireCall(caller.borrow_as<fio::Directory>())
                          ->Watch(mask, 0, std::move(endpoints->server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result->s, ZX_OK);

  WatchBuffer wb;
  memset(&wb, 0, sizeof(wb));

  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  fbl::unique_fd fd(openat(dirfd(dir), "foo", O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "foo", fio::wire::WatchEvent::kAdded));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  ASSERT_EQ(rename(GetPath("dir/foo").c_str(), GetPath("dir/bar").c_str()), 0);

  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "foo", fio::wire::WatchEvent::kRemoved));
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "bar", fio::wire::WatchEvent::kAdded));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  ASSERT_EQ(unlink(GetPath("dir/bar").c_str()), 0);
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, "bar", fio::wire::WatchEvent::kRemoved));
  ASSERT_NO_FATAL_FAILURE(CheckForEmpty(&wb, endpoints->client));

  // The fd is still owned by "dir".
  caller.release().release();
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir(GetPath("dir").c_str()), 0) << errno;
}

TEST_P(WatcherTest, DirectoryDeleted) {
  if (!fs().GetTraits().supports_watch_event_deleted) {
    std::cout << "Skipping " << fs().GetTraits().name << std::endl;
    return;
  }
  std::string dir_name = GetPath("dir");
  ASSERT_EQ(mkdir(dir_name.c_str(), 0666), 0);
  DIR* dir = opendir(dir_name.c_str());
  ASSERT_NE(dir, nullptr);

  {
    zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
    ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

    fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
    auto watch_result =
        fidl::WireCall(caller.borrow_as<fio::Directory>())
            ->Watch(fio::wire::WatchMask::kDeleted, 0, std::move(endpoints->server));
    ASSERT_EQ(watch_result.status(), ZX_OK);
    ASSERT_EQ(watch_result->s, ZX_OK);
    std::string dir2_name = GetPath("dir2");
    ASSERT_EQ(mkdir(dir2_name.c_str(), 0666), 0);

    // Renaming over a directory should generate a deleted directory event.
    ASSERT_EQ(rename(dir2_name.c_str(), dir_name.c_str()), 0);

    WatchBuffer wb = {};
    ASSERT_NO_FATAL_FAILURE(
        CheckForEvent(&wb, endpoints->client, ".", fio::wire::WatchEvent::kDeleted));
  }

  closedir(dir);
  dir = opendir(dir_name.c_str());
  ASSERT_NE(dir, nullptr);

  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  fdio_cpp::FdioCaller caller(fbl::unique_fd(dirfd(dir)));
  auto watch_result = fidl::WireCall(caller.borrow_as<fio::Directory>())
                          ->Watch(fio::wire::WatchMask::kDeleted, 0, std::move(endpoints->server));
  ASSERT_EQ(watch_result.status(), ZX_OK);
  ASSERT_EQ(watch_result->s, ZX_OK);

  // Unlinking a directory should generate a deleted directory event.
  ASSERT_EQ(rmdir(dir_name.c_str()), 0);

  WatchBuffer wb = {};
  ASSERT_NO_FATAL_FAILURE(
      CheckForEvent(&wb, endpoints->client, ".", fio::wire::WatchEvent::kDeleted));

  closedir(dir);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, WatcherTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
