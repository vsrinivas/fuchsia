// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/entity/entity_watcher_impl.h"
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

// Creates an entity with a valid type and data and verifies they are returned
// as expected.
TEST_F(StoryStorageTest, CreateAndReadEntity) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });

  bool read_entity_type{};
  storage->GetEntityType(cookie)->Then([&](StoryStorage::Status status, std::string type) {
    EXPECT_EQ(type, expected_type);
    read_entity_type = true;
  });
  RunLoopUntil([&] { return read_entity_type; });

  bool read_entity_data{};
  storage->GetEntityData(cookie, expected_type)
      ->Then([&](StoryStorage::Status status, fuchsia::mem::BufferPtr buffer) {
        EXPECT_TRUE(buffer);
        std::string read_data;
        EXPECT_TRUE(fsl::StringFromVmo(*buffer, &read_data));
        EXPECT_EQ(read_data, data_string);
        read_entity_data = true;
      });
  RunLoopUntil([&] { return read_entity_data; });
}

// Creates an entity with a valid type and data and attempts to get the data of
// a different type.
TEST_F(StoryStorageTest, CreateAndReadEntityIncorrectType) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });

  bool read_entity_data{};
  storage->GetEntityData(cookie, expected_type + expected_type)
      ->Then([&](StoryStorage::Status status, fuchsia::mem::BufferPtr buffer) {
        EXPECT_EQ(status, StoryStorage::Status::INVALID_ENTITY_TYPE);
        read_entity_data = true;
      });
  RunLoopUntil([&] { return read_entity_data; });
}

// Creates an entity with a valid type and data and attempts to get the data of
// a different cookie.
TEST_F(StoryStorageTest, CreateAndReadEntityIncorrectCookie) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });

  bool read_entity_data{};
  storage->GetEntityData(cookie + cookie, expected_type)
      ->Then([&](StoryStorage::Status status, fuchsia::mem::BufferPtr buffer) {
        EXPECT_EQ(status, StoryStorage::Status::INVALID_ENTITY_COOKIE);
        read_entity_data = true;
      });
  RunLoopUntil([&] { return read_entity_data; });
}

// Attempts to create an entity with an empty type and verifies it fails.
TEST_F(StoryStorageTest, CreateEntityWithEmptyType) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::INVALID_ENTITY_TYPE);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });
}

// Attempts to create an entity with an empty cookie and verifies it fails.
TEST_F(StoryStorageTest, CreateEntityWithEmptyCookie) {
  auto storage = CreateStorage("page");
  std::string cookie = "";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::INVALID_ENTITY_COOKIE);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });
}

// Creates an entity and performs a second write with a different type, and
// verifies the second write fails and that the second attempted write doesn't
// corrupt the data.
TEST_F(StoryStorageTest, WriteEntityDataWithIncorrectType) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";
  std::string incorrect_type = "com.fuchsia.test.incorrect.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });

  bool wrote_entity{};
  storage->SetEntityData(cookie, incorrect_type, fuchsia::mem::Buffer())
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::INVALID_ENTITY_TYPE);
        wrote_entity = true;
      });
  RunLoopUntil([&] { return wrote_entity; });

  // Verify that the second write didn't mess up the data or type.
  bool read_entity_type{};
  storage->GetEntityType(cookie)->Then([&](StoryStorage::Status status, std::string type) {
    EXPECT_EQ(type, expected_type);
    read_entity_type = true;
  });
  RunLoopUntil([&] { return read_entity_type; });

  bool read_entity_data{};
  storage->GetEntityData(cookie, expected_type)
      ->Then([&](StoryStorage::Status status, fuchsia::mem::BufferPtr buffer) {
        EXPECT_TRUE(buffer);
        std::string read_data;
        EXPECT_TRUE(fsl::StringFromVmo(*buffer, &read_data));
        EXPECT_EQ(read_data, data_string);
        read_entity_data = true;
      });
  RunLoopUntil([&] { return read_entity_data; });
}

// Creates an entity and performs a second write with the same type, and
// verifies the data is written correctly.
TEST_F(StoryStorageTest, WriteToEntityTwice) {
  auto storage = CreateStorage("page");
  std::string cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  std::string second_data_string = "more_test_data";
  fuchsia::mem::Buffer second_buffer;
  FXL_CHECK(fsl::VmoFromString(second_data_string, &second_buffer));

  bool created_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });

  bool wrote_entity{};
  storage->SetEntityData(cookie, expected_type, std::move(second_buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        wrote_entity = true;
      });
  RunLoopUntil([&] { return wrote_entity; });

  // Verify that the second write successfully updated the data.
  bool read_entity_type{};
  storage->GetEntityType(cookie)->Then([&](StoryStorage::Status status, std::string type) {
    EXPECT_EQ(type, expected_type);
    read_entity_type = true;
  });
  RunLoopUntil([&] { return read_entity_type; });

  bool read_entity_data{};
  storage->GetEntityData(cookie, expected_type)
      ->Then([&](StoryStorage::Status status, fuchsia::mem::BufferPtr buffer) {
        EXPECT_TRUE(buffer);
        std::string read_data;
        EXPECT_TRUE(fsl::StringFromVmo(*buffer, &read_data));
        EXPECT_EQ(read_data, second_data_string);
        read_entity_data = true;
      });
  RunLoopUntil([&] { return read_entity_data; });
}

// Creates an entity with a watcher and verifies that updates to the entity are
// delivered to the watcher.
TEST_F(StoryStorageTest, WatchEntityData) {
  auto storage = CreateStorage("page");
  std::string expected_cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool saw_entity_update{};
  bool saw_entity_update_with_no_data{};
  auto watcher_impl = EntityWatcherImpl([&](std::unique_ptr<fuchsia::mem::Buffer> value) {
    // Verify that the first callback is called with no data, since the
    // entity data has yet to be set.
    if (!value && !saw_entity_update_with_no_data) {
      saw_entity_update_with_no_data = true;
      return;
    }

    ASSERT_TRUE(value) << "Saw multiple empty entity updates.";

    std::string read_data;
    EXPECT_TRUE(fsl::StringFromVmo(*value, &read_data));
    EXPECT_EQ(read_data, data_string);

    saw_entity_update = true;
  });

  fuchsia::modular::EntityWatcherPtr watcher_ptr;
  watcher_impl.Connect(watcher_ptr.NewRequest());

  storage->WatchEntity(expected_cookie, expected_type, std::move(watcher_ptr));

  bool created_entity{};
  storage->SetEntityData(expected_cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });
  RunLoopUntil([&] { return saw_entity_update; });
  EXPECT_TRUE(saw_entity_update_with_no_data);
}

// Creates an entity with multiple watchers and verifies that updates to the
// entity are delivered to all watchers.
TEST_F(StoryStorageTest, WatchEntityDataMultipleWatchers) {
  auto storage = CreateStorage("page");
  std::string expected_cookie = "cookie";
  std::string expected_type = "com.fuchsia.test.type";

  std::string data_string = "test_data";
  fuchsia::mem::Buffer buffer;
  FXL_CHECK(fsl::VmoFromString(data_string, &buffer));

  bool saw_entity_update{};
  auto watcher_impl = EntityWatcherImpl([&](std::unique_ptr<fuchsia::mem::Buffer> value) {
    if (!value) {
      // The first update may not contain any data, so skip it.
      return;
    }

    std::string read_data;
    EXPECT_TRUE(fsl::StringFromVmo(*value, &read_data));
    EXPECT_EQ(read_data, data_string);

    saw_entity_update = true;
  });

  fuchsia::modular::EntityWatcherPtr watcher_ptr;
  watcher_impl.Connect(watcher_ptr.NewRequest());
  storage->WatchEntity(expected_cookie, expected_type, std::move(watcher_ptr));

  bool saw_entity_update_too{};
  auto second_watcher_impl = EntityWatcherImpl([&](std::unique_ptr<fuchsia::mem::Buffer> value) {
    if (!value) {
      // The first update may not contain any data, so skip it.
      return;
    }

    std::string read_data;
    EXPECT_TRUE(fsl::StringFromVmo(*value, &read_data));
    EXPECT_EQ(read_data, data_string);

    saw_entity_update_too = true;
  });

  fuchsia::modular::EntityWatcherPtr second_watcher_ptr;
  second_watcher_impl.Connect(second_watcher_ptr.NewRequest());
  storage->WatchEntity(expected_cookie, expected_type, std::move(second_watcher_ptr));

  bool created_entity{};
  storage->SetEntityData(expected_cookie, expected_type, std::move(buffer))
      ->Then([&](StoryStorage::Status status) {
        EXPECT_EQ(status, StoryStorage::Status::OK);
        created_entity = true;
      });
  RunLoopUntil([&] { return created_entity; });
  RunLoopUntil([&] { return saw_entity_update && saw_entity_update_too; });
}

// Creates a name for a given Entity cookie and verifies it is retrieved
// successfully.
TEST_F(StoryStorageTest, NameEntity) {
  auto storage = CreateStorage("page");
  std::string expected_cookie = "cookie";
  std::string expected_name = "the best entity";

  storage->SetEntityName(expected_cookie, expected_name);

  bool did_get_cookie{};
  storage->GetEntityCookieForName(expected_name)
      ->Then([&](StoryStorage::Status status, const std::string& cookie) {
        EXPECT_EQ(cookie, expected_cookie);
        did_get_cookie = true;
      });
  RunLoopUntil([&] { return did_get_cookie; });
}

}  // namespace
}  // namespace modular
