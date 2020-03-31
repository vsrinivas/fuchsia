// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/ledger_client/page_id.h"
#include "src/modular/lib/testing/test_with_ledger.h"

using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {
namespace {

class StoryStorageTest : public modular_testing::TestWithLedger {
 protected:
  std::unique_ptr<StoryStorage> CreateStorage(std::string page_id) {
    return std::make_unique<StoryStorage>(ledger_client(), modular::MakePageId(page_id));
  }
};

ModuleData Clone(const ModuleData& data) {
  ModuleData dup;
  data.Clone(&dup);
  return dup;
}

TEST_F(StoryStorageTest, ReadModuleData_NonexistentModule) {
  auto storage = CreateStorage("page");

  bool read_done{};
  std::vector<std::string> path;
  path.push_back("a");
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_FALSE(data);
  });

  RunLoopUntil([&] { return read_done; });
}

TEST_F(StoryStorageTest, ReadAllModuleData_Empty) {
  auto storage = CreateStorage("page");

  bool read_done{};
  fidl::VectorPtr<ModuleData> all_module_data;
  storage->ReadAllModuleData()->Then([&](std::vector<ModuleData> data) {
    read_done = true;
    all_module_data.emplace(std::move(data));
  });

  RunLoopUntil([&] { return read_done; });
  ASSERT_TRUE(all_module_data);
  EXPECT_EQ(0u, all_module_data->size());
}

TEST_F(StoryStorageTest, WriteReadModuleData) {
  // Write and then read some ModuleData entries. We expect to get the same data
  // back.
  auto storage = CreateStorage("page");

  int notification_count{0};
  storage->set_on_module_data_updated([&](ModuleData) { notification_count++; });

  ModuleData module_data1;
  module_data1.set_module_url("url1");
  module_data1.mutable_module_path()->push_back("path1");
  storage->WriteModuleData(Clone(module_data1));

  ModuleData module_data2;
  module_data2.set_module_url("url2");
  module_data2.mutable_module_path()->push_back("path2");
  storage->WriteModuleData(Clone(module_data2));

  // We don't need to explicitly wait on WriteModuleData() because the
  // implementation: 1) serializes all storage operations and 2) guarantees the
  // WriteModuleData() action is finished only once the data has been written.
  ModuleData read_data1;
  bool read1_done{};
  storage->ReadModuleData(module_data1.module_path())->Then([&](ModuleDataPtr data) {
    read1_done = true;
    ASSERT_TRUE(data);
    read_data1 = std::move(*data);
  });

  ModuleData read_data2;
  bool read2_done{};
  storage->ReadModuleData(module_data2.module_path())->Then([&](ModuleDataPtr data) {
    read2_done = true;
    ASSERT_TRUE(data);
    read_data2 = std::move(*data);
  });

  RunLoopUntil([&] { return read1_done && read2_done; });
  EXPECT_TRUE(fidl::Equals(module_data1, read_data1));
  EXPECT_TRUE(fidl::Equals(module_data2, read_data2));

  // Read the same data back with ReadAllModuleData().
  fidl::VectorPtr<ModuleData> all_module_data;
  storage->ReadAllModuleData()->Then(
      [&](std::vector<ModuleData> data) { all_module_data.emplace(std::move(data)); });
  RunLoopUntil([&] { return !!all_module_data; });
  EXPECT_EQ(2u, all_module_data->size());
  EXPECT_TRUE(fidl::Equals(module_data1, all_module_data->at(0)));
  EXPECT_TRUE(fidl::Equals(module_data2, all_module_data->at(1)));

  // We should get a notification every time module data is updated.
  EXPECT_EQ(2, notification_count);
}

TEST_F(StoryStorageTest, UpdateModuleData) {
  // Call UpdateModuleData() on a record that doesn't exist yet.
  auto storage = CreateStorage("page");

  // We're going to observe changes on another storage instance, which
  // simulates another device.
  auto other_storage = CreateStorage("page");
  bool got_notification{};
  ModuleData notified_module_data;
  other_storage->set_on_module_data_updated([&](ModuleData data) {
    got_notification = true;
    notified_module_data = std::move(data);
  });

  std::vector<std::string> path;
  path.push_back("a");

  // Case 1: Don't mutate anything.
  bool update_done{};
  storage->UpdateModuleData(path, [](ModuleDataPtr* ptr) { EXPECT_FALSE(*ptr); })->Then([&] {
    update_done = true;
  });
  RunLoopUntil([&] { return update_done; });

  bool read_done{};
  ModuleData read_data;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    EXPECT_FALSE(data);
  });
  RunLoopUntil([&] { return read_done; });
  // Since nothing changed, we should not have seen a notification.
  EXPECT_FALSE(got_notification);

  // Case 2: Initialize an otherwise empty record.
  update_done = false;
  storage
      ->UpdateModuleData(path,
                         [&](ModuleDataPtr* ptr) {
                           EXPECT_FALSE(*ptr);

                           *ptr = ModuleData::New();
                           (*ptr)->set_module_path(path);
                           (*ptr)->set_module_url("foobar");
                         })
      ->Then([&] { update_done = true; });
  RunLoopUntil([&] { return update_done; });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ(path, data->module_path());
    EXPECT_EQ("foobar", data->module_url());
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("foobar", notified_module_data.module_url());

  // Case 3: Leave alone an existing record.
  got_notification = false;
  storage->UpdateModuleData(path, [&](ModuleDataPtr* ptr) { EXPECT_TRUE(*ptr); });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("foobar", data->module_url());
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_FALSE(got_notification);

  // Case 4: Mutate an existing record.
  storage->UpdateModuleData(path, [&](ModuleDataPtr* ptr) {
    EXPECT_TRUE(*ptr);
    (*ptr)->set_module_url("baz");
  });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("baz", data->module_url());
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("baz", notified_module_data.module_url());
}

namespace {
LinkPath MakeLinkPath(const std::string& name) {
  LinkPath path;
  path.link_name = name;
  return path;
}
}  // namespace

TEST_F(StoryStorageTest, GetLink_Null) {
  auto storage = CreateStorage("page");

  // Default for an un-set Link is to get a "null" back.
  bool get_done{};
  fidl::StringPtr value;
  storage->GetLinkValue(MakeLinkPath("link"))
      ->Then([&](StoryStorage::Status status, fidl::StringPtr v) {
        EXPECT_EQ(StoryStorage::Status::OK, status);
        value = v;
        get_done = true;
      });
  RunLoopUntil([&] { return get_done; });
  EXPECT_EQ("null", value);
}

TEST_F(StoryStorageTest, UpdateLinkValue) {
  auto storage = CreateStorage("page");

  // Let's set a value.
  int mutate_count{0};
  int context;
  storage
      ->UpdateLinkValue(
          MakeLinkPath("link"),
          [](fidl::StringPtr* current_value) {
            EXPECT_FALSE(current_value->has_value());
            *current_value = "10";
          },
          &context)
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(StoryStorage::Status::OK, status);
        ++mutate_count;
      });

  // If we mutate again, we should see the old value.
  storage
      ->UpdateLinkValue(
          MakeLinkPath("link"),
          [](fidl::StringPtr* current_value) {
            EXPECT_EQ("10", *current_value);
            *current_value = "20";
          },
          &context)
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(StoryStorage::Status::OK, status);
        ++mutate_count;
      });

  // Now let's fetch it and see the newest value.
  bool get_done{};
  fidl::StringPtr value;
  storage->GetLinkValue(MakeLinkPath("link"))
      ->Then([&](StoryStorage::Status status, fidl::StringPtr v) {
        EXPECT_EQ(StoryStorage::Status::OK, status);
        value = v;
        get_done = true;
      });
  RunLoopUntil([&] { return get_done; });

  EXPECT_EQ(2, mutate_count);
  EXPECT_EQ("20", value);
}

}  // namespace
}  // namespace modular
