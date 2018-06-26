// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/storage/story_storage.h"

#include <memory>

#include "gtest/gtest.h"
#include "lib/async/cpp/future.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {
namespace {

class StoryStorageTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<StoryStorage> CreateStorage(std::string page_id) {
    return std::make_unique<StoryStorage>(ledger_client(), MakePageId(page_id));
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
  fidl::VectorPtr<fidl::StringPtr> path;
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
  storage->ReadAllModuleData()->Then([&](fidl::VectorPtr<ModuleData> data) {
    read_done = true;
    all_module_data = std::move(data);
  });

  RunLoopUntil([&] { return read_done; });
  ASSERT_TRUE(all_module_data);
  EXPECT_EQ(0u, all_module_data->size());
}

TEST_F(StoryStorageTest, WriteReadModuleData) {
  // Write and then read some ModuleData entries. We expect to get the same data
  // back.
  auto storage = CreateStorage("page");

  bool got_notification{};
  storage->set_on_module_data_updated(
      [&](ModuleData) { got_notification = true; });

  ModuleData module_data1;
  module_data1.module_url = "url1";
  module_data1.module_path.push_back("path1");
  storage->WriteModuleData(Clone(module_data1));

  ModuleData module_data2;
  module_data2.module_url = "url2";
  module_data2.module_path.push_back("path2");
  storage->WriteModuleData(Clone(module_data2));

  // We don't need to explicitly wait on WriteModuleData() because the
  // implementation: 1) serializes all storage operations and 2) guarantees the
  // WriteModuleData() action is finished only once the data has been written.
  ModuleData read_data1;
  bool read1_done{};
  storage->ReadModuleData(module_data1.module_path)
      ->Then([&](ModuleDataPtr data) {
        read1_done = true;
        ASSERT_TRUE(data);
        read_data1 = std::move(*data);
      });

  ModuleData read_data2;
  bool read2_done{};
  storage->ReadModuleData(module_data2.module_path)
      ->Then([&](ModuleDataPtr data) {
        read2_done = true;
        ASSERT_TRUE(data);
        read_data2 = std::move(*data);
      });

  RunLoopUntil([&] { return read1_done && read2_done; });
  EXPECT_EQ(module_data1, read_data1);
  EXPECT_EQ(module_data2, read_data2);

  // Read the same data back with ReadAllModuleData().
  fidl::VectorPtr<ModuleData> all_module_data;
  storage->ReadAllModuleData()->Then([&](fidl::VectorPtr<ModuleData> data) {
    all_module_data = std::move(data);
  });
  RunLoopUntil([&] { return !!all_module_data; });
  EXPECT_EQ(2u, all_module_data->size());
  EXPECT_EQ(module_data1, all_module_data->at(0));
  EXPECT_EQ(module_data2, all_module_data->at(1));

  // At no time should we have gotten a notification about ModuleData records
  // from this storage instance.
  EXPECT_FALSE(got_notification);
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

  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("a");

  // Case 1: Don't mutate anything.
  bool update_done{};
  storage
      ->UpdateModuleData(path, [](ModuleDataPtr* ptr) { EXPECT_FALSE(*ptr); })
      ->Then([&] { update_done = true; });
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
                           (*ptr)->module_path = path.Clone();
                           (*ptr)->module_url = "foobar";
                         })
      ->Then([&] { update_done = true; });
  RunLoopUntil([&] { return update_done; });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ(path, data->module_path);
    EXPECT_EQ("foobar", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("foobar", notified_module_data.module_url);

  // Case 3: Leave alone an existing record.
  got_notification = false;
  storage->UpdateModuleData(path,
                            [&](ModuleDataPtr* ptr) { EXPECT_TRUE(*ptr); });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("foobar", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_FALSE(got_notification);

  // Case 4: Mutate an existing record.
  storage->UpdateModuleData(path, [&](ModuleDataPtr* ptr) {
    EXPECT_TRUE(*ptr);
    (*ptr)->module_url = "baz";
  });

  read_done = false;
  storage->ReadModuleData(path)->Then([&](ModuleDataPtr data) {
    read_done = true;
    ASSERT_TRUE(data);
    EXPECT_EQ("baz", data->module_url);
  });
  RunLoopUntil([&] { return read_done; });
  // Now something changed, so we should see a notification.
  EXPECT_TRUE(got_notification);
  EXPECT_EQ("baz", notified_module_data.module_url);
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
      ->UpdateLinkValue(MakeLinkPath("link"),
                        [](fidl::StringPtr* current_value) {
                          EXPECT_EQ("null", *current_value);
                          *current_value = "10";
                        },
                        &context)
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(StoryStorage::Status::OK, status);
        ++mutate_count;
      });

  // If we mutate again, we should see the old value.
  storage
      ->UpdateLinkValue(MakeLinkPath("link"),
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

TEST_F(StoryStorageTest, WatchingLink_IgnoresOthers) {
  // When we watch a link, we should see changes only for that link.
  auto storage = CreateStorage("page");

  // We'll be watching "foo", but updating "bar".
  int notified_count{0};
  auto cancel =
      storage->WatchLink(MakeLinkPath("foo"),
                         [&](const fidl::StringPtr& value,
                             const void* /* context */) { ++notified_count; });

  bool mutate_done{};
  int context;
  storage
      ->UpdateLinkValue(MakeLinkPath("bar"),
                        [](fidl::StringPtr* value) { *value = "10"; }, &context)
      ->Then([&](StoryStorage::Status status) { mutate_done = true; });
  RunLoopUntil([&] { return mutate_done; });
  EXPECT_EQ(0, notified_count);
}

TEST_F(StoryStorageTest, WatchingLink_IgnoresNoopUpdates) {
  // When we watch a link, we should see changes only for that link.
  auto storage = CreateStorage("page");

  int notified_count{0};
  auto cancel =
      storage->WatchLink(MakeLinkPath("foo"),
                         [&](const fidl::StringPtr& value,
                             const void* /* context */) { ++notified_count; });

  bool mutate_done{};
  int context;
  storage
      ->UpdateLinkValue(MakeLinkPath("foo"),
                        [](fidl::StringPtr* value) { /* do nothing */ },
                        &context)
      ->Then([&](StoryStorage::Status status) { mutate_done = true; });
  RunLoopUntil([&] { return mutate_done; });
  EXPECT_EQ(0, notified_count);
}

TEST_F(StoryStorageTest, WatchingLink_SeesUpdates) {
  // When we make changes to Link values, we should see those changes in our
  // observation functions. When we cancel the observer, we shouldn't see any
  // more notifications.
  auto storage = CreateStorage("page");

  // We'll tell StoryStorage to stop notifying us about "bar" later by using
  // |bar_cancel|.
  int notified_count{0};
  fidl::StringPtr notified_value;
  const void* notified_context;
  auto watch_cancel = storage->WatchLink(
      MakeLinkPath("bar"),
      [&](const fidl::StringPtr& value, const void* context) {
        ++notified_count;
        notified_value = value;
        notified_context = context;
      });

  // Change "bar"'s value to "10".
  bool mutate_done{};
  int context;
  storage
      ->UpdateLinkValue(MakeLinkPath("bar"),
                        [](fidl::StringPtr* value) { *value = "10"; }, &context)
      ->Then([&](StoryStorage::Status status) { mutate_done = true; });
  RunLoopUntil([&] { return mutate_done; });
  EXPECT_EQ(1, notified_count);
  EXPECT_EQ("10", notified_value);
  EXPECT_EQ(&context, notified_context);

  // Change it two more times. We expect to be notified of the first one, but
  // not the second because we are going to cancel our watcher.
  storage
      ->UpdateLinkValue(MakeLinkPath("bar"),
                        [](fidl::StringPtr* value) { *value = "20"; }, &context)
      ->Then([&](StoryStorage::Status status) {
        watch_cancel.call();  // Remove the watcher for bar.
      });

  mutate_done = false;
  storage
      ->UpdateLinkValue(MakeLinkPath("bar"),
                        [](fidl::StringPtr* value) { *value = "30"; }, &context)
      ->Then([&](StoryStorage::Status status) { mutate_done = true; });
  RunLoopUntil([&] { return mutate_done; });

  EXPECT_EQ(2, notified_count);
  EXPECT_EQ("20", notified_value);
  EXPECT_EQ(&context, notified_context);
}

TEST_F(StoryStorageTest, WatchingOtherStorageInstance) {
  // Observations made on other StoryStorage instances get a special nullptr
  // context.
  auto storage = CreateStorage("page");

  auto other_storage = CreateStorage("page");

  int notified_count{0};
  fidl::StringPtr notified_value;
  const void* notified_context;
  auto watch_cancel = other_storage->WatchLink(
      MakeLinkPath("foo"),
      [&](const fidl::StringPtr& value, const void* context) {
        ++notified_count;
        notified_value = value;
        notified_context = context;
        return true;
      });

  int context;
  storage->UpdateLinkValue(MakeLinkPath("foo"),
                           [](fidl::StringPtr* value) { *value = "10"; },
                           &context);

  RunLoopUntil([&] { return notified_count > 0; });
  EXPECT_EQ(1, notified_count);
  EXPECT_EQ("10", notified_value);
  EXPECT_EQ(nullptr, notified_context);
}

TEST_F(StoryStorageTest, SetInvalidValue) {
  // Try to set a value to null or invalid json.
  auto storage = CreateStorage("page");

  // We shouldn't see any notifications for errors.
  int notified_count_foo{0};
  auto watch_cancel = storage->WatchLink(
      MakeLinkPath("foo"),
      [&](const fidl::StringPtr& value, const void* /* context */) {
        ++notified_count_foo;
        return true;
      });

  int context;
  bool mutate_done{};
  storage
      ->UpdateLinkValue(MakeLinkPath("foo"),
                        [](fidl::StringPtr* value) { (*value).reset(); },
                        &context)
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(StoryStorage::Status::LINK_INVALID_JSON, status);
        mutate_done = true;
      });
  RunLoopUntil([&] { return mutate_done; });

  mutate_done = false;
  storage
      ->UpdateLinkValue(MakeLinkPath("foo"),
                        [](fidl::StringPtr* value) { *value = "not json"; },
                        &context)
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(StoryStorage::Status::LINK_INVALID_JSON, status);
        mutate_done = true;
      });
  RunLoopUntil([&] { return mutate_done; });

  EXPECT_EQ(0, notified_count_foo);
}

}  // namespace
}  // namespace modular
