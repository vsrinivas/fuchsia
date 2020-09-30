// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/gtest/real_loop_fixture.h>
#include <zircon/errors.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/array_to_string.h"

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

TEST_F(SessionStorageTest, Create_VerifyData_NoAnnotations) {
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

TEST_F(SessionStorageTest, ObserveCreateUpdateDelete) {
  auto storage = CreateStorage();

  bool updated{false};
  std::string updated_story_name;
  fuchsia::modular::internal::StoryData updated_story_data;
  storage->SubscribeStoryUpdated(
      [&](std::string story_name, const fuchsia::modular::internal::StoryData& story_data) {
        updated_story_name = std::move(story_name);
        updated_story_data = fidl::Clone(story_data);
        updated = true;
        return WatchInterest::kContinue;
      });

  bool deleted{false};
  std::string deleted_story_name;
  storage->SubscribeStoryDeleted([&](std::string story_name) {
    deleted_story_name = std::move(story_name);
    deleted = true;
    return WatchInterest::kContinue;
  });

  auto created_story_name = storage->CreateStory("story", {});
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);
  EXPECT_EQ(created_story_name, updated_story_data.story_info().id());

  // Update something and see a new notification.
  updated = false;
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(created_story_name, std::move(annotations));
  RunLoopUntil([&] { return updated; });
  EXPECT_EQ(created_story_name, updated_story_name);

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

// Verifies that an on_annotations_updated callback is invoked when annotations are added/merged,
// with the correct story_name and updated annotations.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedCallback) {
  static constexpr auto story_name = "story";

  auto storage = CreateStorage();

  // Create a story with no annotations.
  storage->CreateStory(story_name, /*annotations=*/{});

  bool updated{};
  std::string updated_story_id;
  std::vector<fuchsia::modular::Annotation> updated_annotations;
  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        updated_story_id = story_id;
        updated_annotations = std::move(annotations);
        updated = true;
      });

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(story_name, std::move(annotations));

  EXPECT_TRUE(updated);
  EXPECT_EQ(story_name, updated_story_id);
  ASSERT_EQ(1u, updated_annotations.size());
  EXPECT_THAT(updated_annotations.at(0), annotations::AnnotationEq(ByRef(annotation)));
}

// Verifies that multiple on_annotations_updated for a single story callbacks are called
// when annotations are added/merged.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedMultipleCallbacks) {
  static constexpr auto story_name = "story";
  static constexpr auto num_callbacks = 5;

  auto storage = CreateStorage();

  // Create a story with no annotations.
  storage->CreateStory(story_name, /*annotations=*/{});

  auto updated_count = 0;
  auto callback = [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
    updated_count++;
  };

  for (int i = 0; i < num_callbacks; i++) {
    storage->add_on_annotations_updated_once(story_name, callback);
  }

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(story_name, std::move(annotations));

  EXPECT_EQ(num_callbacks, updated_count);
}

// Verifies that an on_annotations_updated callback registered with a particular story_id
// is invoked when only that story's annotations are updated.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedMultipleStories) {
  static constexpr auto story_name = "story-updated";
  static constexpr auto story_name_no_update = "story-not-updated";

  auto storage = CreateStorage();

  // Create a story with no annotations.
  storage->CreateStory(story_name, /*annotations=*/{});

  storage->add_on_annotations_updated_once(
      story_name_no_update,
      [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        FX_NOTREACHED() << "Callback called for story " << story_id << " but was registered for "
                        << story_name_no_update;
      });

  bool updated{};
  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        updated = true;
      });

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(story_name, std::move(annotations));

  // Only the callback for |story_name| should have been called.
  EXPECT_TRUE(updated);
}

// Verifies that an on_annotations_updated callback is removed when the story is deleted.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedCallbackRemoveOnDelete) {
  static constexpr auto story_name = "story";

  auto storage = CreateStorage();

  // Create a story with no annotations.
  storage->CreateStory(story_name, /*annotations=*/{});

  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        FX_NOTREACHED() << "Callback should have been removed on story delete";
      });

  // Delete the story and create one with the same name.
  // The new story should not have any previous callbacks.
  storage->DeleteStory(story_name);
  storage->CreateStory(story_name, /*annotations=*/{});

  // Add a different callback.
  bool updated{};
  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        updated = true;
      });

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text("test_annotation_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_annotation_key", .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(story_name, std::move(annotations));

  // The second callback should have been called.
  EXPECT_TRUE(updated);
}

// Verifies that an on_annotations_updated callback is called only once when annotations are
// updated multiple times.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedCallbackCalledOnce) {
  static constexpr auto story_name = "story";

  auto storage = CreateStorage();

  // Create a story with no annotations.
  storage->CreateStory(story_name, /*annotations=*/{});

  // Add a callback.
  int updated_count = 0;
  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        updated_count++;
      });

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> first_annotations{};
  fuchsia::modular::AnnotationValue first_annotation_value;
  first_annotation_value.set_text("first_test_annotation_value");
  auto first_annotation =
      fuchsia::modular::Annotation{.key = "first_test_annotation_key",
                                   .value = fidl::MakeOptional(std::move(first_annotation_value))};
  first_annotations.push_back(fidl::Clone(first_annotation));

  storage->MergeStoryAnnotations(story_name, std::move(first_annotations));

  // The callback should have been called.
  EXPECT_EQ(1, updated_count);

  // Annotate the story again.
  std::vector<fuchsia::modular::Annotation> second_annotations{};
  fuchsia::modular::AnnotationValue second_annotation_value;
  second_annotation_value.set_text("second_test_annotation_value");
  auto second_annotation =
      fuchsia::modular::Annotation{.key = "second_test_annotation_key",
                                   .value = fidl::MakeOptional(std::move(second_annotation_value))};
  second_annotations.push_back(fidl::Clone(second_annotation));

  storage->MergeStoryAnnotations(story_name, std::move(second_annotations));

  // The callback should not have been called again.
  EXPECT_EQ(1, updated_count);
}

// Verifies that an on_annotations_updated callback for a story that does not yet exist is
// only called when the annotations are updated.
TEST_F(SessionStorageTest, OnAnnotationsUpdatedCallbackBeforeCreate) {
  static constexpr auto story_name = "story";

  auto storage = CreateStorage();

  // Add a callback.
  bool updated{};
  int annotations_count = 0;
  storage->add_on_annotations_updated_once(
      story_name, [&](std::string story_id, std::vector<fuchsia::modular::Annotation> annotations) {
        updated = true;
        annotations_count = annotations.size();
      });

  // Create a story with some annotations.
  std::vector<fuchsia::modular::Annotation> first_annotations{};
  fuchsia::modular::AnnotationValue first_annotation_value;
  first_annotation_value.set_text("first_test_annotation_value");
  auto first_annotation =
      fuchsia::modular::Annotation{.key = "first_test_annotation_key",
                                   .value = fidl::MakeOptional(std::move(first_annotation_value))};
  first_annotations.push_back(fidl::Clone(first_annotation));

  storage->CreateStory(story_name, std::move(first_annotations));

  // The callback should not have been invoked.
  EXPECT_FALSE(updated);
  EXPECT_EQ(0, annotations_count);

  // Annotate the story.
  std::vector<fuchsia::modular::Annotation> second_annotations{};
  fuchsia::modular::AnnotationValue second_annotation_value;
  second_annotation_value.set_text("second_test_annotation_value");
  auto second_annotation =
      fuchsia::modular::Annotation{.key = "second_test_annotation_key",
                                   .value = fidl::MakeOptional(std::move(second_annotation_value))};
  second_annotations.push_back(fidl::Clone(second_annotation));

  storage->MergeStoryAnnotations(story_name, std::move(second_annotations));

  // The callback should have been invoked.
  EXPECT_TRUE(updated);
  EXPECT_EQ(2, annotations_count);
}

// Tests that multiple watchers passed to SubscribeStoryUpdated are notified with the same
// data when the story is updated.
TEST_F(SessionStorageTest, SubscribeStoryUpdatedMultipleWatchers) {
  static constexpr auto kTestStoryName = "story_name";
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  auto storage = CreateStorage();

  auto story_id = storage->CreateStory(kTestStoryName, /*annotations=*/{});

  bool is_first_watcher_called{false};
  storage->SubscribeStoryUpdated(
      [&, expected_story_id = story_id](std::string story_id,
                                        const fuchsia::modular::internal::StoryData& story_data) {
        EXPECT_EQ(expected_story_id, story_id);
        EXPECT_EQ(expected_story_id, story_data.story_info().id());
        EXPECT_TRUE(story_data.story_info().has_annotations());
        is_first_watcher_called = true;
        return WatchInterest::kStop;
      });

  bool is_second_watcher_called{false};
  storage->SubscribeStoryUpdated(
      [&, expected_story_id = story_id](std::string story_id,
                                        const fuchsia::modular::internal::StoryData& story_data) {
        EXPECT_EQ(expected_story_id, story_id);
        EXPECT_EQ(expected_story_id, story_data.story_info().id());
        EXPECT_TRUE(story_data.story_info().has_annotations());
        is_second_watcher_called = true;
        return WatchInterest::kStop;
      });

  // Update the story to trigger the watchers.
  std::vector<fuchsia::modular::Annotation> annotations{};
  fuchsia::modular::AnnotationValue annotation_value;
  annotation_value.set_text(kTestAnnotationValue);
  auto annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey, .value = fidl::MakeOptional(std::move(annotation_value))};
  annotations.push_back(fidl::Clone(annotation));

  storage->MergeStoryAnnotations(story_id, std::move(annotations));

  RunLoopUntil([&] { return is_first_watcher_called && is_second_watcher_called; });
}

// Tests that multiple watchers passed to SubscribeStoryDeleted are notified with the same
// data when the story is deleted.
TEST_F(SessionStorageTest, SubscribeStoryDeletedMultipleWatchers) {
  static constexpr auto kTestStoryName = "story_name";

  auto storage = CreateStorage();

  auto story_id = storage->CreateStory(kTestStoryName, /*annotations=*/{});

  bool is_first_watcher_called{false};
  storage->SubscribeStoryDeleted([&, expected_story_id = story_id](std::string story_id) {
    EXPECT_EQ(expected_story_id, story_id);
    is_first_watcher_called = true;
    return WatchInterest::kStop;
  });

  bool is_second_watcher_called{false};
  storage->SubscribeStoryDeleted([&, expected_story_id = story_id](std::string story_id) {
    EXPECT_EQ(expected_story_id, story_id);
    is_second_watcher_called = true;
    return WatchInterest::kStop;
  });

  storage->DeleteStory(story_id);

  RunLoopUntil([&] { return is_first_watcher_called && is_second_watcher_called; });
}

}  // namespace
}  // namespace modular
