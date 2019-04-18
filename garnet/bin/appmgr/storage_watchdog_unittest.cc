// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/storage_watchdog.h"

#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/memfs/memfs.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#define EXAMPLE_PATH \
  "/hippo_storage/cache/r/sys/fuchsia.com:cobalt:0#meta:cobalt.cmx"
#define EXAMPLE_TEST_PATH              \
  "/hippo_storage/cache/r/sys/r/test/" \
  "fuchsia.com:cobalt-unittest:0#meta:cobalt-unittest.cmx"

#define TMPDATA "abcdefghijklmnopqrstuvwxyz1234567890"
#define TMPDATA_SIZE 36

class StorageWatchdogTest : public ::testing::Test {
 public:
  StorageWatchdogTest() {}
};

TEST_F(StorageWatchdogTest, Basic) {
  auto loop = new async::Loop(&kAsyncLoopConfigAttachToThread);
  ASSERT_TRUE(ZX_OK == memfs_install_at_with_page_limit(loop->dispatcher(), 5,
                                                        "/hippo_storage"));
  ASSERT_TRUE(ZX_OK == loop->StartThread());

  files::CreateDirectory(EXAMPLE_PATH);
  files::CreateDirectory(EXAMPLE_TEST_PATH);

  StorageWatchdog *watchdog =
      new StorageWatchdog("/hippo_storage", "/hippo_storage/cache");
  EXPECT_TRUE(95 > watchdog->GetStorageUsage());

  // fill up the storage
  int counter = 0;
  while (true) {
    auto filename = std::to_string(counter++);
    if (!files::WriteFile(files::JoinPath(EXAMPLE_PATH, filename), TMPDATA,
                          TMPDATA_SIZE))
      break;
    if (!files::WriteFile(files::JoinPath(EXAMPLE_TEST_PATH, filename), TMPDATA,
                          TMPDATA_SIZE))
      break;
  }

  EXPECT_TRUE(95 < watchdog->GetStorageUsage());
  watchdog->PurgeCache();

  std::vector<std::string> example_files = {};
  EXPECT_TRUE(files::ReadDirContents(EXAMPLE_PATH, &example_files));
  EXPECT_EQ(1ul, example_files.size());
  EXPECT_TRUE(example_files.at(0).compare(".") == 0);

  std::vector<std::string> example_test_files = {};
  EXPECT_TRUE(files::ReadDirContents(EXAMPLE_TEST_PATH, &example_test_files));
  EXPECT_EQ(1ul, example_test_files.size());
  EXPECT_TRUE(example_test_files.at(0).compare(".") == 0);
}
