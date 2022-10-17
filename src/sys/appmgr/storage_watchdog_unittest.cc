// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_watchdog.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/namespace.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sync/completion.h>

#include <algorithm>
#include <string>
#include <vector>

#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/storage/memfs/scoped_memfs.h"

namespace {

namespace fio = fuchsia_io;

const char* kTmpData = "abcdefghijklmnopqrstuvwxyz1234567890";

class StorageWatchdogTest : public ::testing::Test {
 public:
  StorageWatchdogTest() : loop_(async::Loop(&kAsyncLoopConfigAttachToCurrentThread)) {}

  void SetUp() override {
    testing::Test::SetUp();

    ASSERT_EQ(ZX_OK, loop_.StartThread());

    zx::result<ScopedMemfs> memfs =
        ScopedMemfs::CreateMountedAt(loop_.dispatcher(), "/hippo_storage");
    ASSERT_TRUE(memfs.is_ok());
    memfs_ = std::make_unique<ScopedMemfs>(std::move(*memfs));
  }

  void TearDown() override {
    memfs_->set_cleanup_timeout(zx::sec(5));
    memfs_.reset();
  }

 private:
  async::Loop loop_;
  std::unique_ptr<ScopedMemfs> memfs_;
};

class TestStorageWatchdog : public StorageWatchdog {
 public:
  TestStorageWatchdog(std::string path_to_watch, std::string path_to_clean)
      : StorageWatchdog(inspect::Node(), path_to_watch, path_to_clean) {}

  zx_status_t GetFilesystemInfo(zx_handle_t directory, fio::wire::FilesystemInfo* out_info) {
    if (directory == ZX_HANDLE_INVALID) {
      return ZX_ERR_BAD_HANDLE;
    }
    *out_info = info;
    return ZX_OK;
  }

  fio::wire::FilesystemInfo info = {};
};

TEST_F(StorageWatchdogTest, Basic) {
  const std::string kRootPath = "/hippo_storage/cache";
  const std::string kRealmPath = files::JoinPath(kRootPath, "r/sys");
  const std::string kNestedRealmPath = files::JoinPath(kRootPath, "r/sys/r/test");
  const std::string kV1Path = files::JoinPath(kRealmPath, "fuchsia.com:cobalt:0#meta:cobalt.cmx");
  const std::string kV1NestedPath =
      files::JoinPath(kNestedRealmPath, "fuchsia.com:cobalt-unittest:0#meta:cobalt-unittest.cmx");
  const std::string kV2IdPath = files::JoinPath(
      kRootPath, "e5ef2bbe9dd2b7cee87beac5e06cece13fe6f9c154b1f00abec155e6c6c9fa62");
  const std::string kV2MonikerBasePath = files::JoinPath(kRootPath, "network:0");
  const std::string kV2MonikerPath = files::JoinPath(kV2MonikerBasePath, "data");
  const std::string kV2NestedMonikerBasePath =
      files::JoinPath(kRootPath, "network:0/children/netstack:0");
  const std::string kV2NestedMonikerPath = files::JoinPath(kV2NestedMonikerBasePath, "data");

  TestStorageWatchdog watchdog = TestStorageWatchdog("/hippo_storage", "/hippo_storage/cache");
  watchdog.info.used_bytes = 0;
  watchdog.info.total_bytes = 20 * 1024;
  auto usage = watchdog.GetStorageUsage();
  EXPECT_LE(usage.percent(), StorageWatchdog::kCachePurgeThresholdPct);

  for (size_t i = 0; i < 10; ++i) {
    auto filename = std::to_string(i);
    for (const std::string& path :
         {kV1Path, kV1NestedPath, kV2IdPath, kV2MonikerPath, kV2NestedMonikerPath}) {
      ASSERT_TRUE(files::CreateDirectory(path));
      ASSERT_TRUE(files::WriteFile(files::JoinPath(path, filename), kTmpData, strlen(kTmpData)));
    }
  }

  watchdog.info.used_bytes = watchdog.info.total_bytes - 128;

  // Confirm that storage pressure is high, clear the cache, check that things
  // were actually deleted (but the directories themselves were preserved).
  usage = watchdog.GetStorageUsage();
  EXPECT_GT(usage.percent(), StorageWatchdog::kCachePurgeThresholdPct);
  watchdog.PurgeCache();

  // For each case, check:
  // - The contents of the storage dir are cleared.
  // - The storage dir itself is not deleted.

  // V1
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV1Path, &files));
    EXPECT_EQ(1ul, files.size());
    EXPECT_EQ(files[0], ".");
  }
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kRealmPath, &files));
    EXPECT_EQ(3ul, files.size());
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files[0], ".");
    EXPECT_EQ(files[1], "fuchsia.com:cobalt:0#meta:cobalt.cmx");
    EXPECT_EQ(files[2], "r");
  }

  // V1 nested
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV1NestedPath, &files));
    EXPECT_EQ(1ul, files.size());
    EXPECT_EQ(files[0], ".");
  }
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kNestedRealmPath, &files));
    EXPECT_EQ(2ul, files.size());
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files[0], ".");
    EXPECT_EQ(files[1], "fuchsia.com:cobalt-unittest:0#meta:cobalt-unittest.cmx");
  }

  // V2 instance id
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV2IdPath, &files));
    EXPECT_EQ(1ul, files.size());
    EXPECT_EQ(files[0], ".");
  }
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kRootPath, &files));
    EXPECT_EQ(4ul, files.size());
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files[0], ".");
    EXPECT_EQ(files[1], "e5ef2bbe9dd2b7cee87beac5e06cece13fe6f9c154b1f00abec155e6c6c9fa62");
    EXPECT_EQ(files[2], "network:0");
    EXPECT_EQ(files[3], "r");
  }

  // V2 moniker
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV2MonikerPath, &files));
    EXPECT_EQ(1ul, files.size());
    EXPECT_EQ(files[0], ".");
  }
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV2MonikerBasePath, &files));
    EXPECT_EQ(3ul, files.size());
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files[0], ".");
    EXPECT_EQ(files[1], "children");
    EXPECT_EQ(files[2], "data");
  }

  // V2 nested moniker
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV2NestedMonikerPath, &files));
    EXPECT_EQ(1ul, files.size());
    EXPECT_EQ(files[0], ".");
  }
  {
    std::vector<std::string> files = {};
    EXPECT_TRUE(files::ReadDirContents(kV2NestedMonikerBasePath, &files));
    EXPECT_EQ(2ul, files.size());
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files[0], ".");
    EXPECT_EQ(files[1], "data");
  }
}

}  // namespace
