// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/optional.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/ledger_client/page_id.h"
#include "src/modular/lib/testing/test_with_ledger.h"
#include "zircon/system/public/zircon/errors.h"

namespace modular {
namespace {

using ::testing::ByRef;

class SessionStorageTest : public modular_testing::TestWithLedger {
 protected:
  std::unique_ptr<SessionStorage> CreateStorage(std::string page_id) {
    return std::make_unique<SessionStorage>(ledger_client(), modular::MakePageId(page_id));
  }

  // Convenience method to create a story for the test cases where
  // we're not testing CreateStory().
  fidl::StringPtr CreateStory(SessionStorage* storage) {
    auto future_story = storage->CreateStory(/*name=*/nullptr, /*annotations=*/{});
    bool done{};
    fidl::StringPtr story_name;
    future_story->Then([&](fidl::StringPtr name, fuchsia::ledger::PageId page_id) {
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

  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto future_story = storage->CreateStory("story_name", std::move(annotations));
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
    ASSERT_TRUE(story_name.has_value());
    EXPECT_EQ(story_name.value(), data->story_info().id());

    EXPECT_TRUE(data->story_info().has_annotations());
    EXPECT_EQ(1u, data->story_info().annotations().size());
    EXPECT_THAT(data->story_info().annotations().at(0),
                annotations::AnnotationEq(ByRef(annotation)));

    ASSERT_TRUE(data->has_story_page_id());
    EXPECT_TRUE(fidl::Equals(to_string(page_id.id), data->story_page_id()));

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
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

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
  auto future_story = storage->CreateStory("story_name", /*annotations=*/{});

  // Immediately after creation is complete, delete it.
  FuturePtr<> delete_done;
  future_story->Then([&](fidl::StringPtr story_name, fuchsia::ledger::PageId page_id) {
    delete_done = storage->DeleteStory(story_name);
  });

  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });

  RunLoopUntil([&] { return all_data.has_value(); });

  // Given the ordering, we expect the story we created to show up.
  EXPECT_EQ(1u, all_data->size());

  // But if we get all data again, we should see no stories.
  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });
  EXPECT_EQ(0u, all_data->size());
}

TEST_F(SessionStorageTest, CreateMultipleAndDeleteOne) {
  // Create two stories.
  //
  // * Their ids should be different.
  // * They should get different Ledger page ids.
  // * If we GetAllStoryData() we should see both of them.
  auto storage = CreateStorage("page");

  auto future_story1 = storage->CreateStory("story1", /*annotations=*/{});
  auto future_story2 = storage->CreateStory("story2", /*annotations=*/{});

  fidl::StringPtr story1_name;
  fuchsia::ledger::PageId story1_pageid;
  fidl::StringPtr story2_name;
  fuchsia::ledger::PageId story2_pageid;
  bool done = false;
  Wait("SessionStorageTest.CreateMultipleAndDeleteOne.wait", {future_story1, future_story2})
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
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

  EXPECT_EQ(2u, all_data->size());

  // Now delete one of them, and we should see that GetAllStoryData() only
  // returns one entry.
  bool delete_done{};
  storage->DeleteStory("story1")->Then([&] { delete_done = true; });

  future_all_data = storage->GetAllStoryData();
  all_data.reset();
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

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

TEST_F(SessionStorageTest, CreateSameStoryOnlyOnce) {
  // Call CreateStory twice with the same story name, but with annotations only in the first call.
  // Both calls should succeed, and the second call should be a no-op:
  //
  //   * The story should only be created once.
  //   * The second call should return the same story name and page ID as the first.
  //   * The final StoryData should contain annotations from the first call.
  auto storage = CreateStorage("page");

  // Only the first CreateStory call has annotations.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto future_story_first = storage->CreateStory("story", std::move(annotations));
  auto future_story_second = storage->CreateStory("story", /*annotations=*/{});

  fidl::StringPtr story_first_name;
  fuchsia::ledger::PageId story_first_pageid;
  fidl::StringPtr story_second_name;
  fuchsia::ledger::PageId story_second_pageid;
  bool done = false;
  Wait("SessionStorageTest.CreateSameStoryOnlyOnce.wait", {future_story_first, future_story_second})
      ->Then([&](auto results) {
        story_first_name = std::move(std::get<0>(results[0]));
        story_first_pageid = std::move(std::get<1>(results[0]));
        story_second_name = std::move(std::get<0>(results[1]));
        story_second_pageid = std::move(std::get<1>(results[1]));
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Both calls should return the same name and page ID because they refer to the same story.
  EXPECT_EQ(story_first_name, story_second_name);
  EXPECT_TRUE(fidl::Equals(story_first_pageid, story_second_pageid));

  // Only one story should have been created.
  auto future_all_data = storage->GetAllStoryData();
  fidl::VectorPtr<fuchsia::modular::internal::StoryData> all_data;
  future_all_data->Then([&](std::vector<fuchsia::modular::internal::StoryData> data) {
    all_data.emplace(std::move(data));
  });
  RunLoopUntil([&] { return all_data.has_value(); });

  EXPECT_EQ(1u, all_data->size());

  // The story should have the annotation from the first call to CreateStory.
  const auto& story_info = all_data->at(0).story_info();
  EXPECT_TRUE(story_info.has_annotations());
  EXPECT_EQ(1u, story_info.annotations().size());
  EXPECT_THAT(story_info.annotations().at(0), annotations::AnnotationEq(ByRef(annotation)));
}

// TODO(MF-420): This test is racy: the |story_page| acquired here is different
// from the one used internally to delete page data. The call to GetSnapshot()
// may happen before, after, or during, the process of deleting the page
// contents.
TEST_F(SessionStorageTest, DISABLED_DeleteStoryDeletesStoryPage) {
  // When we call DeleteStory, we expect the story's page to be completely
  // emptied.
  auto storage = CreateStorage("page");
  auto future_story = storage->CreateStory("story_name", /*annotations=*/{});

  bool done{false};
  auto story_page_id = fuchsia::ledger::PageId::New();
  future_story->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) {
    *story_page_id = std::move(page_id);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Add some fake content to the story's page, so that we can show that
  // it is deleted when we instruct SessionStorage to delete the story.
  fuchsia::ledger::PagePtr story_page;
  story_page.set_error_handler(
      [](zx_status_t status) { FAIL() << "Unexpected disconnection on page, status: " << status; });
  ledger_client()->ledger()->GetPage(std::move(story_page_id), story_page.NewRequest());
  done = false;
  story_page->Put(modular::to_array("key"), modular::to_array("value"));
  story_page->Sync([&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Delete the story.
  done = false;
  storage->DeleteStory("story_name")->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Show that the underlying page is now empty.
  fuchsia::ledger::PageSnapshotPtr snapshot;
  story_page->GetSnapshot(snapshot.NewRequest(), modular::to_array("") /* prefix */,
                          nullptr /* watcher */);
  done = false;
  snapshot->GetEntries(
      modular::to_array("") /* key_start */, nullptr /* token */,
      [&](std::vector<fuchsia::ledger::Entry> entries, fuchsia::ledger::TokenPtr next_token) {
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
    EXPECT_EQ(10, data->story_info().last_focus_time());
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
      [&](fidl::StringPtr story_name, fuchsia::modular::internal::StoryData story_data) {
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
  ASSERT_TRUE(created_story_name.has_value());
  EXPECT_EQ(created_story_name.value(), updated_story_data.story_info().id());

  // Update something and see a new notification.
  updated = false;
  storage->UpdateLastFocusedTimestamp(created_story_name, 42);
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(42, updated_story_data.story_info().last_focus_time());

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
      [&](fidl::StringPtr story_name, fuchsia::modular::internal::StoryData story_data) {
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
  ASSERT_TRUE(created_story_name.has_value());
  EXPECT_EQ(created_story_name.value(), updated_story_data.story_info().id());

  // Update something and see a new notification.
  updated = false;
  remote_storage->UpdateLastFocusedTimestamp(created_story_name, 42);
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(42, updated_story_data.story_info().last_focus_time());

  // Delete the story and expect to see a notification.
  remote_storage->DeleteStory(created_story_name);
  RunLoopUntil([&] { return deleted; });
  EXPECT_EQ(created_story_name, deleted_story_name);
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

}  // namespace
}  // namespace modular
