// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/gtest/real_loop_fixture.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "zircon/system/public/zircon/errors.h"

namespace modular {
namespace {

using ::testing::ByRef;

class SessionStorageTest : public gtest::RealLoopFixture {
 protected:
  std::unique_ptr<SessionStorage> CreateStorage() { return std::make_unique<SessionStorage>(); }
};

TEST_F(SessionStorageTest, Create_VerifyData) {
  // Create a single story, and verify that the data we have stored about it is
  // correct.
  auto storage = CreateStorage();

  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto story_name = storage->CreateStory("story_name", std::move(annotations));

  // Get the StoryData for this story.
  auto cached_data = storage->GetStoryData(story_name);
  ASSERT_TRUE(cached_data);

  EXPECT_EQ("story_name", cached_data->story_name());
  EXPECT_EQ(story_name, cached_data->story_info().id());

  EXPECT_TRUE(cached_data->story_info().has_annotations());
  EXPECT_EQ(1u, cached_data->story_info().annotations().size());
  EXPECT_THAT(cached_data->story_info().annotations().at(0),
              annotations::AnnotationEq(ByRef(annotation)));

  // Get the StoryData again, but this time by its name.
  auto data = storage->GetStoryData("story_name");
  ASSERT_TRUE(data);
  ASSERT_TRUE(fidl::Equals(*cached_data, *data));

  // Verify that GetAllStoryData() also returns the same information.
  auto all_data = storage->GetAllStoryData();
  EXPECT_EQ(1u, all_data.size());
  EXPECT_TRUE(fidl::Equals(*cached_data, all_data.at(0)));
}

TEST_F(SessionStorageTest, Create_VerifyData_NoAnntations) {
  // Create a single story with no annotations, and verify that the data we have stored about it is
  // correct.
  auto storage = CreateStorage();

  storage->CreateStory("story_name", {});
  // Get the StoryData for this story.
  auto data = storage->GetStoryData("story_name");
  ASSERT_TRUE(data);

  EXPECT_EQ("story_name", data->story_name());
  EXPECT_EQ("story_name", data->story_info().id());

  EXPECT_TRUE(data->story_info().has_annotations());
  EXPECT_EQ(0u, data->story_info().annotations().size());
}

TEST_F(SessionStorageTest, CreateGetAllDelete) {
  // Create a single story, call GetAllStoryData() to show that it was created,
  // and then delete it.
  //
  // Since the implementation has switched from an asynchronous one to a
  // synchronous one in asynchronous clothing, don't rely on Future ordering for
  // consistency.  Rely only on function call ordering.  We'll switch the
  // interface to be blocking in a future commit.
  auto storage = CreateStorage();
  storage->CreateStory("story_name", /*annotations=*/{});

  auto all_data = storage->GetAllStoryData();
  EXPECT_EQ(1u, all_data.size());

  // Then, delete it.
  storage->DeleteStory("story_name");

  // But if we get all data again, we should see no stories.
  all_data = storage->GetAllStoryData();
  EXPECT_EQ(0u, all_data.size());
}

TEST_F(SessionStorageTest, CreateMultipleAndDeleteOne) {
  // Create two stories.
  //
  // * Their ids should be different.
  // * They should get different names.
  // * If we GetAllStoryData() we should see both of them.
  auto storage = CreateStorage();

  auto story1_name = storage->CreateStory("story1", /*annotations=*/{});
  auto story2_name = storage->CreateStory("story2", /*annotations=*/{});

  EXPECT_NE(story1_name, story2_name);

  auto all_data = storage->GetAllStoryData();
  EXPECT_EQ(2u, all_data.size());

  // Now delete one of them, and we should see that GetAllStoryData() only
  // returns one entry.
  storage->DeleteStory("story1");

  all_data = storage->GetAllStoryData();
  EXPECT_EQ(1u, all_data.size());

  // If we try to get the story by id, or by name, we expect both to return
  // null.
  EXPECT_TRUE(storage->GetStoryData(story1_name) == nullptr);
  EXPECT_TRUE(storage->GetStoryData("story1") == nullptr);
}

TEST_F(SessionStorageTest, CreateSameStoryOnlyOnce) {
  // Call CreateStory twice with the same story name, but with annotations only in the first call.
  // Both calls should succeed, and the second call should be a no-op:
  //
  //   * The story should only be created once.
  //   * The second call should return the same story name as the first.
  //   * The final StoryData should contain annotations from the first call.
  auto storage = CreateStorage();

  // Only the first CreateStory call has annotations.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  auto story_first_name = storage->CreateStory("story", std::move(annotations));
  auto story_second_name = storage->CreateStory("story", /*annotations=*/{});

  // Both calls should return the same name because they refer to the same story.
  EXPECT_EQ(story_first_name, story_second_name);

  // Only one story should have been created.
  auto all_data = storage->GetAllStoryData();
  EXPECT_EQ(1u, all_data.size());

  // The story should have the annotation from the first call to CreateStory.
  const auto& story_info = all_data.at(0).story_info();
  EXPECT_TRUE(story_info.has_annotations());
  EXPECT_EQ(1u, story_info.annotations().size());
  EXPECT_THAT(story_info.annotations().at(0), annotations::AnnotationEq(ByRef(annotation)));
}

TEST_F(SessionStorageTest, UpdateLastFocusedTimestamp) {
  auto storage = CreateStorage();
  auto story_name = storage->CreateStory("story", {});

  storage->UpdateLastFocusedTimestamp(story_name, 10);
  auto data = storage->GetStoryData(story_name);
  EXPECT_EQ(10, data->story_info().last_focus_time());
}

TEST_F(SessionStorageTest, ObserveCreateUpdateDelete) {
  auto storage = CreateStorage();

  bool updated{};
  std::string updated_story_name;
  fuchsia::modular::internal::StoryData updated_story_data;
  storage->set_on_story_updated(
      [&](std::string story_name, fuchsia::modular::internal::StoryData story_data) {
        updated_story_name = std::move(story_name);
        updated_story_data = std::move(story_data);
        updated = true;
      });

  bool deleted{};
  std::string deleted_story_name;
  storage->set_on_story_deleted([&](std::string story_name) {
    deleted_story_name = std::move(story_name);
    deleted = true;
  });

  auto created_story_name = storage->CreateStory("story", {});
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id());

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

  // Once a story is already deleted, do not expect another
  // notification.
  deleted = false;
  storage->DeleteStory(created_story_name);
  EXPECT_EQ(false, deleted);
}

TEST_F(SessionStorageTest, GetStoryStorage) {
  auto storage = CreateStorage();
  auto story_name = storage->CreateStory("story", {});
  EXPECT_NE(nullptr, storage->GetStoryStorage(story_name));
}

TEST_F(SessionStorageTest, GetStoryStorageNoStory) {
  auto storage = CreateStorage();
  storage->CreateStory("story", {});
  EXPECT_EQ(nullptr, storage->GetStoryStorage("fake"));
}

}  // namespace
}  // namespace modular
