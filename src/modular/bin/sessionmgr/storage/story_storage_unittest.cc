// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage.h"

#include <lib/gtest/real_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/async/cpp/future.h"

using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {
namespace {

class StoryStorageTest : public gtest::RealLoopFixture {
 protected:
  std::unique_ptr<StoryStorage> CreateStorage() { return std::make_unique<StoryStorage>(); }
};

ModuleData Clone(const ModuleData& data) {
  ModuleData dup;
  data.Clone(&dup);
  return dup;
}

TEST_F(StoryStorageTest, ReadModuleData_NonexistentModule) {
  auto storage = CreateStorage();

  std::vector<std::string> path;
  path.push_back("a");
  auto data = storage->ReadModuleData(path);
  ASSERT_FALSE(data);
}

TEST_F(StoryStorageTest, ReadAllModuleData_Empty) {
  auto storage = CreateStorage();
  auto all_module_data = storage->ReadAllModuleData();
  EXPECT_EQ(0u, all_module_data.size());
}

TEST_F(StoryStorageTest, WriteReadModuleData) {
  // Write and then read some ModuleData entries. We expect to get the same data
  // back.
  auto storage = CreateStorage();

  int notification_count_all_changes{0};
  int notification_count_one_change{0};
  storage->SubscribeModuleDataUpdated(
      [&notification_count_all_changes](const ModuleData& /* unused */) {
        notification_count_all_changes++;
        // Continue receiving notifications
        return WatchInterest::kContinue;
      });

  storage->SubscribeModuleDataUpdated(
      [&notification_count_one_change](const ModuleData& /* unused */) {
        notification_count_one_change++;
        EXPECT_EQ(1, notification_count_one_change);
        // Stop receiving notifications
        return WatchInterest::kStop;
      });

  ModuleData module_data1;
  module_data1.set_module_url("url1");
  module_data1.mutable_module_path()->push_back("path1");
  storage->WriteModuleData(Clone(module_data1));

  ModuleData module_data2;
  module_data2.set_module_url("url2");
  module_data2.mutable_module_path()->push_back("path2");
  storage->WriteModuleData(Clone(module_data2));

  auto read_data1 = storage->ReadModuleData(module_data1.module_path());
  ASSERT_TRUE(read_data1);
  auto read_data2 = storage->ReadModuleData(module_data2.module_path());
  ASSERT_TRUE(read_data2);

  EXPECT_TRUE(fidl::Equals(module_data1, *read_data1));
  EXPECT_TRUE(fidl::Equals(module_data2, *read_data2));

  // Read the same data back with ReadAllModuleData().
  auto all_module_data = storage->ReadAllModuleData();
  EXPECT_EQ(2u, all_module_data.size());
  EXPECT_TRUE(fidl::Equals(module_data1, all_module_data.at(0)));
  EXPECT_TRUE(fidl::Equals(module_data2, all_module_data.at(1)));

  // We should get a notification every time module data is updated for the
  // first subscription.
  EXPECT_EQ(2, notification_count_all_changes);
  // The second subscription should terminate after the first time it receives a
  // callback, and should only see one change.
  EXPECT_EQ(1, notification_count_one_change);
}

TEST_F(StoryStorageTest, MarkModuleAsDeleted) {
  auto storage = CreateStorage();

  // Try to make a non-existent module as deleted.
  EXPECT_FALSE(storage->MarkModuleAsDeleted({"a"}));

  ModuleData module_data;
  module_data.set_module_url("url1");
  module_data.mutable_module_path()->push_back("a");
  storage->WriteModuleData(Clone(module_data));

  int notification_count = 0;
  ModuleData notified_data;
  storage->SubscribeModuleDataUpdated([&](const ModuleData& module_data) {
    ++notification_count;
    notified_data = fidl::Clone(module_data);
    return WatchInterest::kContinue;
  });

  EXPECT_TRUE(storage->MarkModuleAsDeleted(module_data.module_path()));
  EXPECT_EQ(1, notification_count);
  EXPECT_EQ(true, notified_data.module_deleted());

  auto read_data = storage->ReadModuleData(module_data.module_path());
  EXPECT_TRUE(read_data);
  EXPECT_EQ(true, read_data->module_deleted());

  // Marking it deleted again resulted in no change, hence expect no
  // new notifications.
  EXPECT_TRUE(storage->MarkModuleAsDeleted(module_data.module_path()));
  EXPECT_EQ(1, notification_count);
}

}  // namespace
}  // namespace modular
