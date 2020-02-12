// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_watchdog.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/namespace.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>

#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#define EXAMPLE_PATH "/hippo_storage/cache/r/sys/fuchsia.com:cobalt:0#meta:cobalt.cmx"
#define EXAMPLE_TEST_PATH              \
  "/hippo_storage/cache/r/sys/r/test/" \
  "fuchsia.com:cobalt-unittest:0#meta:cobalt-unittest.cmx"

#define TMPDATA "abcdefghijklmnopqrstuvwxyz1234567890"
#define TMPDATA_SIZE 36

namespace {

class StorageWatchdogTest : public ::testing::Test {
 public:
  StorageWatchdogTest() : loop_(async::Loop(&kAsyncLoopConfigAttachToCurrentThread)) {}

  void SetUp() override {
    testing::Test::SetUp();
    ASSERT_EQ(ZX_OK,
              memfs_create_filesystem(loop_.dispatcher(), &memfs_handle_, &memfs_root_handle_));
    ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&ns_));
    ASSERT_EQ(ZX_OK, fdio_ns_bind(ns_, "/hippo_storage", memfs_root_handle_));

    ASSERT_EQ(ZX_OK, loop_.StartThread());
  }
  // Set up the async loop, create memfs, install memfs at /hippo_storage
  void TearDown() override {
    // Unbind memfs from our namespace, free memfs
    ASSERT_EQ(ZX_OK, fdio_ns_unbind(ns_, "/hippo_storage"));

    sync_completion_t memfs_freed_signal;
    memfs_free_filesystem(memfs_handle_, &memfs_freed_signal);
    ASSERT_EQ(ZX_OK, sync_completion_wait(&memfs_freed_signal, ZX_SEC(5)));
  }

 private:
  async::Loop loop_;
  memfs_filesystem_t* memfs_handle_;
  zx_handle_t memfs_root_handle_;
  fdio_ns_t* ns_;
};

class TestStorageWatchdog : public StorageWatchdog {
 public:
  TestStorageWatchdog(std::string path_to_watch, std::string path_to_clean)
      : StorageWatchdog(path_to_watch, path_to_clean) {}

  zx_status_t GetFilesystemInfo(zx_handle_t directory, fuchsia_io_FilesystemInfo* out_info) {
    if (directory == ZX_HANDLE_INVALID) {
      return ZX_ERR_BAD_HANDLE;
    }
    *out_info = info;
    return ZX_OK;
  }

  fuchsia_io_FilesystemInfo info = {};
};

TEST_F(StorageWatchdogTest, Basic) {
  // Create directories on memfs
  files::CreateDirectory(EXAMPLE_PATH);
  files::CreateDirectory(EXAMPLE_TEST_PATH);

  TestStorageWatchdog watchdog = TestStorageWatchdog("/hippo_storage", "/hippo_storage/cache");
  watchdog.info.used_bytes = 0;
  watchdog.info.total_bytes = 20 * 1024;
  EXPECT_TRUE(95 > watchdog.GetStorageUsage());

  for (size_t i = 0; i < 10; ++i) {
    auto filename = std::to_string(i);
    ASSERT_TRUE(files::WriteFile(files::JoinPath(EXAMPLE_PATH, filename), TMPDATA, TMPDATA_SIZE));
    ASSERT_TRUE(files::WriteFile(files::JoinPath(EXAMPLE_TEST_PATH, filename), TMPDATA, TMPDATA_SIZE));
  }

  watchdog.info.used_bytes = watchdog.info.total_bytes - 128;

  // Confirm that storage pressure is high, clear the cache, check that things
  // were actually deleted
  EXPECT_TRUE(95 < watchdog.GetStorageUsage());
  watchdog.PurgeCache();

  std::vector<std::string> example_files = {};
  EXPECT_TRUE(files::ReadDirContents(EXAMPLE_PATH, &example_files));
  EXPECT_EQ(1ul, example_files.size());
  EXPECT_TRUE(example_files.at(0).compare(".") == 0);

  std::vector<std::string> example_test_files = {};
  EXPECT_TRUE(files::ReadDirContents(EXAMPLE_TEST_PATH, &example_test_files));
  EXPECT_EQ(1ul, example_test_files.size());
  EXPECT_TRUE(example_test_files.at(0).compare(".") == 0);
}

}  // namespace
