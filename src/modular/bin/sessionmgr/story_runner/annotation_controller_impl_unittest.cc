// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/annotation_controller_impl.h"

#include <fuchsia/element/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

using ::modular::annotations::AnnotationEq;
using ::testing::ByRef;
using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

class AnnotationControllerImplTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage();
  }

  fuchsia::element::AnnotationControllerPtr CreateStoryWithAnnotations(
      const std::string story_name, std::vector<fuchsia::modular::Annotation> annotations) {
    story_id_ = session_storage_->CreateStory(std::move(story_name), std::move(annotations));
    auto annotation_controller_impl =
        std::make_unique<AnnotationControllerImpl>(story_id_, session_storage_.get());
    fuchsia::element::AnnotationControllerPtr annotation_controller;
    annotation_controller_impl->Connect(annotation_controller.NewRequest());
    annotation_controllers_.push_back(std::move(annotation_controller_impl));
    return annotation_controller;
  }

  std::string story_id() const { return story_id_; }
  SessionStorage* session_storage() { return session_storage_.get(); }

 private:
  std::string story_id_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::vector<std::unique_ptr<AnnotationControllerImpl>> annotation_controllers_;
};

// Tests that GetAnnotations returns an empty list of custom_annotations for a story that
// has no annotations.
TEST_F(AnnotationControllerImplTest, GetAnnotationsEmpty) {
  auto annotation_controller = CreateStoryWithAnnotations("annotation-test-story", {});

  std::vector<fuchsia::element::Annotation> annotations;
  bool got_annotations{false};
  annotation_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        annotations = std::move(result.response().annotations);
        got_annotations = true;
      });

  RunLoopUntil([&]() { return got_annotations; });

  EXPECT_TRUE(annotations.empty());
}

// Tests that GetAnnotations returns the existing annotations on a story.
TEST_F(AnnotationControllerImplTest, GetAnnotationsExisting) {
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  auto annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

  std::vector<fuchsia::modular::Annotation> modular_annotations;
  modular_annotations.push_back(std::move(annotation));

  auto annotation_controller =
      CreateStoryWithAnnotations("annotation-test-story", std::move(modular_annotations));

  std::vector<fuchsia::element::Annotation> element_annotations;
  bool got_annotations{false};
  annotation_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        element_annotations = std::move(result.response().annotations);
        got_annotations = true;
      });

  RunLoopUntil([&]() { return got_annotations; });

  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  EXPECT_THAT(element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
}

// Tests that UpateAnnotations sets annotations on the element story.
TEST_F(AnnotationControllerImplTest, SetAnnotationsExisting) {
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  auto annotation_controller = CreateStoryWithAnnotations("annotation-test-story", {});

  // Set annotations.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  std::vector<fuchsia::element::Annotation> annotations_to_add;
  annotations_to_add.push_back(fidl::Clone(element_annotation));

  bool did_update_annotations{false};
  annotation_controller->UpdateAnnotations(
      std::move(annotations_to_add),
      /*annotations_to_delete=*/{},
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });

  RunLoopUntil([&]() { return did_update_annotations; });

  // Read the annotations back and ensure they're the same.
  std::vector<fuchsia::element::Annotation> got_element_annotations;
  bool got_annotations{false};
  annotation_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        got_element_annotations = std::move(result.response().annotations);
        got_annotations = true;
      });

  RunLoopUntil([&]() { return got_annotations; });

  EXPECT_THAT(got_element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
}

// Verifies that WatchAnnotations returns existing annotations on first call.
TEST_F(AnnotationControllerImplTest, WatchAnnotationsExisting) {
  // Create a story with some annotations.
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  auto annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

  std::vector<fuchsia::modular::Annotation> modular_annotations;
  modular_annotations.push_back(fidl::Clone(annotation));

  auto annotation_controller =
      CreateStoryWithAnnotations("annotation-test-story", std::move(modular_annotations));

  // Get the annotations.
  bool done{false};
  std::vector<fuchsia::element::Annotation> element_annotations;
  annotation_controller->WatchAnnotations(
      [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
        ASSERT_TRUE(result.is_response());
        element_annotations = std::move(result.response().annotations);
        done = true;
      });

  RunLoopUntil([&] { return done; });

  auto element_annotation = annotations::ToElementAnnotation(annotation);
  EXPECT_THAT(element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
}

// Verifies that WatchAnnotations called concurrently on two different AnnotationControllers both
// return existing annotations on first call.
TEST_F(AnnotationControllerImplTest, WatchAnnotationsExistingMultipleClients) {
  // Create a story with some annotations.
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  auto annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

  std::vector<fuchsia::modular::Annotation> modular_annotations;
  modular_annotations.push_back(fidl::Clone(annotation));

  // Create two annotation controllers for the same story.
  auto annotation_controller_1 =
      CreateStoryWithAnnotations("annotation-test-story", std::move(modular_annotations));
  auto annotation_controller_2 = CreateStoryWithAnnotations("annotation-test-story", {});

  bool controller_1_done{false};
  bool controller_2_done{false};

  // Have both controllers call `WatchAnnotations` at the same time.
  std::vector<fuchsia::element::Annotation> controller_1_element_annotations;
  annotation_controller_1->WatchAnnotations(
      [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
        ASSERT_TRUE(result.is_response());
        controller_1_element_annotations = std::move(result.response().annotations);
        controller_1_done = true;
      });

  std::vector<fuchsia::element::Annotation> controller_2_element_annotations;
  annotation_controller_2->WatchAnnotations(
      [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
        ASSERT_TRUE(result.is_response());
        controller_2_element_annotations = std::move(result.response().annotations);
        controller_2_done = true;
      });

  // This should also return the current set of annotations for both controller_1 and
  // controller_2 right away, and not hang for updates.
  RunLoopUntil([&] { return controller_1_done && controller_2_done; });

  // Test that both controller annotations match the initial annotation set.
  auto element_annotation = annotations::ToElementAnnotation(annotation);
  EXPECT_THAT(controller_1_element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
  EXPECT_THAT(controller_2_element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
}

// Verifies that WatchAnnotations returns updated annotations on subsequent calls.
TEST_F(AnnotationControllerImplTest, WatchAnnotationsUpdates) {
  static constexpr auto kTestAnnotationKey_1 = "test_annotation_key_1";
  static constexpr auto kTestAnnotationValue_1 = "test_annotation_value_1";
  static constexpr auto kTestAnnotationKey_2 = "test_annotation_key_2";
  static constexpr auto kTestAnnotationValue_2 = "test_annotation_value_2";
  static constexpr auto kTestAnnotationValue_3 = "test_annotation_value_3";

  std::vector<fuchsia::modular::Annotation> modular_annotations;
  auto initial_annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey_1,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue_1))};
  modular_annotations.push_back(fidl::Clone(initial_annotation));

  auto annotation_controller =
      CreateStoryWithAnnotations("annotation-test-story", std::move(modular_annotations));

  // Get the annotations.
  bool first_watch_called{false};
  std::vector<fuchsia::element::Annotation> first_watch_annotations;
  annotation_controller->WatchAnnotations(
      [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
        // Ensure this callback is only called once.
        ASSERT_FALSE(first_watch_called);
        first_watch_called = true;
        ASSERT_TRUE(result.is_response());
        first_watch_annotations = std::move(result.response().annotations);
      });

  RunLoopUntil([&] { return first_watch_called; });

  const fuchsia::element::Annotation first_annotation =
      modular::annotations::ToElementAnnotation(initial_annotation);
  EXPECT_THAT(first_watch_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(first_annotation))));

  {  // Start watching for annotations.
    bool second_watch_called{false};
    std::vector<fuchsia::element::Annotation> second_watch_annotations;
    annotation_controller->WatchAnnotations(
        [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
          // Ensure this callback is only called once.
          ASSERT_FALSE(second_watch_called);
          second_watch_called = true;
          ASSERT_TRUE(result.is_response());
          second_watch_annotations = std::move(result.response().annotations);
        });

    // Add another annotation.
    std::vector<fuchsia::modular::Annotation> new_annotations;
    auto new_annotation = fuchsia::modular::Annotation{
        .key = kTestAnnotationKey_2,
        .value = std::make_unique<fuchsia::modular::AnnotationValue>(
            fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue_2))};
    new_annotations.push_back(fidl::Clone(new_annotation));

    bool done{false};
    auto annotations_to_add = modular::annotations::ToElementAnnotations(new_annotations);
    annotation_controller->UpdateAnnotations(
        std::move(annotations_to_add),
        /*annotations_to_delete=*/{},
        [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
          EXPECT_FALSE(result.is_err());
          done = true;
        });

    RunLoopUntil([&] { return done; });
    // WatchAnnotations should have received the new annotations.
    RunLoopUntil([&] { return second_watch_called; });

    const fuchsia::element::Annotation second_annotation =
        modular::annotations::ToElementAnnotation(new_annotation);
    EXPECT_THAT(second_watch_annotations,
                UnorderedElementsAre(element::annotations::AnnotationEq(ByRef(first_annotation)),
                                     element::annotations::AnnotationEq(ByRef(second_annotation))));
  }

  {  // Change the annotations one more time, but do so before calling WatchAnnotations().
    // The call should still return with the change update.
    std::vector<fuchsia::modular::Annotation> new_annotations;
    auto new_annotation = fuchsia::modular::Annotation{
        .key = kTestAnnotationKey_2,
        .value = std::make_unique<fuchsia::modular::AnnotationValue>(
            fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue_3))};
    new_annotations.push_back(fidl::Clone(new_annotation));

    bool done{false};
    auto annotations_to_add = modular::annotations::ToElementAnnotations(new_annotations);
    annotation_controller->UpdateAnnotations(
        std::move(annotations_to_add),
        /*annotations_to_delete=*/{},
        [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
          EXPECT_FALSE(result.is_err());
          done = true;
        });
    RunLoopUntil([&] { return done; });

    bool third_watch_called{false};
    std::vector<fuchsia::element::Annotation> third_watch_annotations;
    annotation_controller->WatchAnnotations(
        [&](fuchsia::element::AnnotationController_WatchAnnotations_Result result) {
          // Ensure this callback is only called once.
          ASSERT_FALSE(third_watch_called);
          third_watch_called = true;
          ASSERT_TRUE(result.is_response());
          third_watch_annotations = std::move(result.response().annotations);
        });

    // WatchAnnotations should have received the new annotations.
    RunLoopUntil([&] { return third_watch_called; });

    const fuchsia::element::Annotation third_annotation =
        modular::annotations::ToElementAnnotation(new_annotation);
    EXPECT_THAT(third_watch_annotations,
                UnorderedElementsAre(element::annotations::AnnotationEq(ByRef(first_annotation)),
                                     element::annotations::AnnotationEq(ByRef(third_annotation))));
  }
}
}  // namespace
}  // namespace modular
