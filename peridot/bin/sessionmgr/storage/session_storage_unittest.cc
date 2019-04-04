// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/storage/session_storage.h"

#include <memory>

#include <lib/async/cpp/future.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"
#include "zircon/system/public/zircon/errors.h"

namespace modular {
namespace {

class SessionStorageTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<SessionStorage> CreateStorage(std::string page_id) {
    return std::make_unique<SessionStorage>(ledger_client(),
                                            MakePageId(page_id));
  }

  // Convenience method to create a story for the test cases where
  // we're not testing CreateStory().
  fidl::StringPtr CreateStory(
      SessionStorage* storage,
      fuchsia::modular::StoryOptions story_options = {}) {
    auto future_story = storage->CreateStory(
        nullptr /* name */, nullptr /* extra */, std::move(story_options));
    bool done{};
    fidl::StringPtr story_name;
    future_story->Then(
        [&](fidl::StringPtr name, fuchsia::ledger::PageId page_id) {
          done = true;
          story_name = std::move(name);
        });
    RunLoopUntil([&] { return done; });

    return story_name;
  }
};

TEST_F(SessionStorageTest, Create_VerifyData) {
  // Create a single story, and verify that the data we have stored about it is
  // correct.
  auto storage = CreateStorage("page");

  fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_entries;
  fuchsia::modular::StoryInfoExtraEntry entry;
  entry.key = "key1";
  entry.value = "value1";
  extra_entries->push_back(std::move(entry));

  entry.key = "key2";
  entry.value = "value2";
  extra_entries->push_back(std::move(entry));

  fuchsia::modular::StoryOptions story_options;
  story_options.kind_of_proto_story = true;
  auto future_story = storage->CreateStory(
      "story_name", std::move(extra_entries), std::move(story_options));
  bool done{};
  fidl::StringPtr story_name;
  fuchsia::ledger::PageId page_id;
  future_story->Then([&](fidl::StringPtr name, fuchsia::ledger::PageId page) {
    done = true;
    story_name = std::move(name);
    page_id = std::move(page);
  });
  RunLoopUntil([&] { return done; });

  // Get the StoryData for this story.
  auto future_data = storage->GetStoryData(story_name);
  done = false;
  fuchsia::modular::internal::StoryData cached_data;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    ASSERT_TRUE(data);

    EXPECT_EQ("story_name", data->story_name());
    EXPECT_TRUE(data->story_options().kind_of_proto_story);
    EXPECT_EQ(story_name, data->story_info().id);
    ASSERT_TRUE(data->has_story_page_id());
    EXPECT_TRUE(fidl::Equals(page_id, data->story_page_id()));
    EXPECT_TRUE(data->story_info().extra);
    EXPECT_EQ(2u, data->story_info().extra->size());
    EXPECT_EQ("key1", data->story_info().extra->at(0).key);
    EXPECT_EQ("value1", data->story_info().extra->at(0).value);
    EXPECT_EQ("key2", data->story_info().extra->at(1).key);
    EXPECT_EQ("value2", data->story_info().extra->at(1).value);

    done = true;

    cached_data = std::move(*data);
  });
  RunLoopUntil([&] { return done; });

  // Get the StoryData again, but this time by its name.
  future_data = storage->GetStoryData("story_name");
  done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    ASSERT_TRUE(data);
    ASSERT_TRUE(fidl::Equals(cached_data, *data));
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Verify that GetAllStoryData() also returns the same information.
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  auto future_all_data = storage->GetAllStoryData();
  future_all_data->Then(
      [&](std::vector<fuchsia::modular::internal::StoryData> data) {
        all_data.reset(std::move(data));
      });
  RunLoopUntil([&] { return !!all_data; });

  EXPECT_EQ(1u, all_data->size());
  EXPECT_TRUE(fidl::Equals(cached_data, all_data->at(0)));
}

TEST_F(SessionStorageTest, CreateGetAllDelete) {
  // Create a single story, call GetAllStoryData() to show that it was created,
  // and then delete it.
  //
  // Pipeline all the calls such to show that we data consistency based on call
  // order.
  auto storage = CreateStorage("page");
  auto future_story = storage->CreateStory(
      "story_name", nullptr /* extra_info */, {} /* options */);

  // Immediately after creation is complete, delete it.
  FuturePtr<> delete_done;
  future_story->Then(
      [&](fidl::StringPtr story_name, fuchsia::ledger::PageId page_id) {
        delete_done = storage->DeleteStory(story_name);
      });

  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then(
      [&](std::vector<fuchsia::modular::internal::StoryData> data) {
        all_data.reset(std::move(data));
      });

  RunLoopUntil([&] { return !!all_data; });

  // Given the ordering, we expect the story we created to show up.
  EXPECT_EQ(1u, all_data->size());

  // But if we get all data again, we should see no stories.
  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then(
      [&](std::vector<fuchsia::modular::internal::StoryData> data) {
        all_data.reset(std::move(data));
      });
  RunLoopUntil([&] { return !!all_data; });
  EXPECT_EQ(0u, all_data->size());
}

TEST_F(SessionStorageTest, CreateMultipleAndDeleteOne) {
  // Create two stories.
  //
  // * Their ids should be different.
  // * They should get different Ledger page ids.
  // * If we GetAllStoryData() we should see both of them.
  auto storage = CreateStorage("page");

  auto future_story1 = storage->CreateStory("story1", nullptr /* extra_info */,
                                            {} /* options */);
  auto future_story2 =
      storage->CreateStory("story2", nullptr /* extra_info */, {} /* options*/);

  fidl::StringPtr story1_name;
  fuchsia::ledger::PageId story1_pageid;
  fidl::StringPtr story2_name;
  fuchsia::ledger::PageId story2_pageid;
  bool done = false;
  Wait("SessionStorageTest.CreateMultipleAndDeleteOne.wait",
       {future_story1, future_story2})
      ->Then([&](auto results) {
        story1_name = std::move(std::get<0>(results[0]));
        story1_pageid = std::move(std::get<1>(results[0]));
        story2_name = std::move(std::get<0>(results[1]));
        story2_pageid = std::move(std::get<1>(results[1]));
        done = true;
      });
  RunLoopUntil([&] { return done; });

  EXPECT_NE(story1_name, story2_name);
  EXPECT_FALSE(fidl::Equals(story1_pageid, story2_pageid));

  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then(
      [&](std::vector<fuchsia::modular::internal::StoryData> data) {
        all_data.reset(std::move(data));
      });
  RunLoopUntil([&] { return !!all_data; });

  EXPECT_EQ(2u, all_data->size());

  // Now delete one of them, and we should see that GetAllStoryData() only
  // returns one entry.
  bool delete_done{};
  storage->DeleteStory("story1")->Then([&] { delete_done = true; });

  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then(
      [&](std::vector<fuchsia::modular::internal::StoryData> data) {
        all_data.reset(std::move(data));
      });
  RunLoopUntil([&] { return !!all_data; });

  EXPECT_TRUE(delete_done);
  EXPECT_EQ(1u, all_data->size());

  // If we try to get the story by id, or by name, we expect both to return
  // null.
  auto future_data = storage->GetStoryData(story1_name);
  done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_TRUE(data == nullptr);
    done = true;
  });

  future_data = storage->GetStoryData("story1");
  done = false;
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_TRUE(data == nullptr);
    done = true;
  });

  // TODO(thatguy): Verify that the story's page was also deleted.
  // MI4-1002
}

TEST_F(SessionStorageTest, DeleteStoryDeletesStoryPage) {
  // When we call DeleteStory, we expect the story's page to be completely
  // emptied.
  auto storage = CreateStorage("page");
  auto future_story = storage->CreateStory(
      "story_name", nullptr /* extra_info */, {} /* options */);

  bool done{false};
  auto story_page_id = fuchsia::ledger::PageId::New();
  future_story->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) {
    *story_page_id = std::move(page_id);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Add some fake content to the story's page, so that we dan show that
  // it is deleted when we instruct SessionStorage to delete the story.
  fuchsia::ledger::PagePtr story_page;
  story_page.set_error_handler([](zx_status_t status) {
    FAIL() << "Unexpected disconnection on page, status: " << status;
  });
  ledger_client()->ledger()->GetPage(std::move(story_page_id),
                                     story_page.NewRequest());
  done = false;
  story_page->PutNew(to_array("key"), to_array("value"));
  story_page->Sync([&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Delete the story.
  done = false;
  storage->DeleteStory("story_name")->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Show that the underlying page is now empty.
  fuchsia::ledger::PageSnapshotPtr snapshot;
  story_page->GetSnapshotNew(snapshot.NewRequest(), to_array("") /* prefix */,
                             nullptr /* watcher */);
  done = false;
  snapshot->GetEntries(to_array("") /* key_start */, nullptr /* token */,
                       [&](fuchsia::ledger::Status status,
                           std::vector<fuchsia::ledger::Entry> entries,
                           fuchsia::ledger::TokenPtr next_token) {
                         ASSERT_EQ(fuchsia::ledger::Status::OK, status);
                         EXPECT_EQ(nullptr, next_token);
                         EXPECT_TRUE(entries.empty());
                         done = true;
                       });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, UpdateLastFocusedTimestamp) {
  auto storage = CreateStorage("page");
  auto story_name = CreateStory(storage.get());

  storage->UpdateLastFocusedTimestamp(story_name, 10);
  auto future_data = storage->GetStoryData(story_name);
  bool done{};
  future_data->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
    EXPECT_EQ(10, data->story_info().last_focus_time);
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, ObserveCreateUpdateDelete_Local) {
  auto storage = CreateStorage("page");

  bool updated{};
  fidl::StringPtr updated_story_name;
  fuchsia::modular::internal::StoryData updated_story_data;
  storage->set_on_story_updated(
      [&](fidl::StringPtr story_name,
          fuchsia::modular::internal::StoryData story_data) {
        updated_story_name = std::move(story_name);
        updated_story_data = std::move(story_data);
        updated = true;
      });

  bool deleted{};
  fidl::StringPtr deleted_story_name;
  storage->set_on_story_deleted([&](fidl::StringPtr story_name) {
    deleted_story_name = std::move(story_name);
    deleted = true;
  });

  auto created_story_name = CreateStory(storage.get());
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id);

  // Update something and see a new notification.
  updated = false;
  storage->UpdateLastFocusedTimestamp(created_story_name, 42);
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(42, updated_story_data.story_info().last_focus_time);

  // Update options and see a new notification.
  updated = false;
  fuchsia::modular::StoryOptions story_options;
  story_options.kind_of_proto_story = true;
  storage->UpdateStoryOptions(created_story_name, std::move(story_options));
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id);
  EXPECT_TRUE(updated_story_data.story_options().kind_of_proto_story);

  // Delete the story and expect to see a notification.
  storage->DeleteStory(created_story_name);
  RunLoopUntil([&] { return deleted; });
  EXPECT_EQ(created_story_name, deleted_story_name);
}

TEST_F(SessionStorageTest, ObserveCreateUpdateDelete_Remote) {
  // Just like above, but we're going to trigger all of the operations that
  // would cause a chagne notification on a different Ledger page connection to
  // simulate them happening on another device.
  auto storage = CreateStorage("page");
  auto remote_storage = CreateStorage("page");

  bool updated{};
  fidl::StringPtr updated_story_name;
  fuchsia::modular::internal::StoryData updated_story_data;
  storage->set_on_story_updated(
      [&](fidl::StringPtr story_name,
          fuchsia::modular::internal::StoryData story_data) {
        updated_story_name = std::move(story_name);
        updated_story_data = std::move(story_data);
        updated = true;
      });

  bool deleted{};
  fidl::StringPtr deleted_story_name;
  storage->set_on_story_deleted([&](fidl::StringPtr story_name) {
    deleted_story_name = std::move(story_name);
    deleted = true;
  });

  auto created_story_name = CreateStory(remote_storage.get());
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id);

  // Update something and see a new notification.
  updated = false;
  remote_storage->UpdateLastFocusedTimestamp(created_story_name, 42);
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(42, updated_story_data.story_info().last_focus_time);

  // Update options and see a new notification.
  updated = false;
  fuchsia::modular::StoryOptions story_options;
  story_options.kind_of_proto_story = true;
  remote_storage->UpdateStoryOptions(created_story_name,
                                     std::move(story_options));
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id);
  EXPECT_TRUE(updated_story_data.story_options().kind_of_proto_story);

  // Delete the story and expect to see a notification.
  remote_storage->DeleteStory(created_story_name);
  RunLoopUntil([&] { return deleted; });
  EXPECT_EQ(created_story_name, deleted_story_name);
}

TEST_F(SessionStorageTest, UpdateStoryOptions) {
  auto storage = CreateStorage("page");
  auto story_name = CreateStory(storage.get());
  bool done{};

  // Start by setting an option.
  fuchsia::modular::StoryOptions story_options;
  story_options.kind_of_proto_story = true;
  storage->UpdateStoryOptions(story_name, std::move(story_options))->Then([&] {
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Read the options (we should only see 1 even when we added 2 since it's the
  // same).
  done = false;
  storage->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_TRUE(data->story_options().kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Update the option again.
  fuchsia::modular::StoryOptions story_options2;
  story_options.kind_of_proto_story = false;
  storage->UpdateStoryOptions(story_name, std::move(story_options2))->Then([&] {
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // We should see the last value we set.
  done = false;
  storage->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_FALSE(data->story_options().kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, GetStoryStorage) {
  auto storage = CreateStorage("page");
  auto story_name = CreateStory(storage.get());

  bool done{};
  auto get_story_future = storage->GetStoryStorage(story_name);
  get_story_future->Then([&](std::unique_ptr<StoryStorage> result) {
    EXPECT_NE(nullptr, result);
    done = true;
  });

  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, GetStoryStorageNoStory) {
  auto storage = CreateStorage("page");
  CreateStory(storage.get());

  bool done{};
  auto get_story_future = storage->GetStoryStorage("fake");
  get_story_future->Then([&](std::unique_ptr<StoryStorage> result) {
    EXPECT_EQ(nullptr, result);
    done = true;
  });

  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, ReadSnapshot) {
  std::string kSnapshotData = "snapshot";

  auto storage = CreateStorage("page");
  auto story_name = CreateStory(storage.get());
  bool done{};

  fsl::SizedVmo snapshot;
  fsl::VmoFromString(kSnapshotData, &snapshot);
  storage->WriteSnapshot(story_name, std::move(snapshot).ToTransport())
      ->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });

  done = false;
  storage->ReadSnapshot(story_name)
      ->Then([&](fuchsia::mem::BufferPtr snapshot_ptr) {
        std::string snapshot_string;
        fsl::StringFromVmo(*snapshot_ptr.get(), &snapshot_string);
        EXPECT_EQ(kSnapshotData, snapshot_string);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SessionStorageTest, DeleteStory_DeletesSnapshot) {
  std::string kSnapshotData = "snapshot";

  auto storage = CreateStorage("page");
  auto story_name = CreateStory(storage.get());
  bool done{};

  fsl::SizedVmo snapshot;
  fsl::VmoFromString(kSnapshotData, &snapshot);
  storage->WriteSnapshot(story_name, std::move(snapshot).ToTransport())
      ->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Deleting the story should delete the snapshot.
  storage->DeleteStory(story_name)->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });

  done = false;
  storage->ReadSnapshot(story_name)
      ->Then([&](fuchsia::mem::BufferPtr snapshot_ptr) {
        EXPECT_TRUE(snapshot_ptr == nullptr);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
