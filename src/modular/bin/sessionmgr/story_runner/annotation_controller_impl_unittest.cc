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

class AnnotationControllerImplTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage();
  }

  void CreateStoryWithAnnotations(std::vector<fuchsia::modular::Annotation> annotations) {
    story_id_ = session_storage_->CreateStory("annotation-test-story", std::move(annotations));
    annotation_controller_impl_ =
        std::make_unique<AnnotationControllerImpl>(story_id_, session_storage_.get());
    annotation_controller_impl_->Connect(annotation_controller_.NewRequest());
  }

  std::string story_id() const { return story_id_; };
  SessionStorage* session_storage() { return session_storage_.get(); }
  fuchsia::element::AnnotationControllerPtr& annotation_controller() {
    return annotation_controller_;
  }

 private:
  std::string story_id_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<AnnotationControllerImpl> annotation_controller_impl_;
  fuchsia::element::AnnotationControllerPtr annotation_controller_;
};

// Tests that GetAnnotations returns an empty list of custom_annotations for a story that
// has no annotations.
TEST_F(AnnotationControllerImplTest, GetAnnotationsEmpty) {
  CreateStoryWithAnnotations({});

  std::vector<fuchsia::element::Annotation> annotations;
  bool got_annotations{false};
  annotation_controller()->GetAnnotations(
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

  CreateStoryWithAnnotations(std::move(modular_annotations));

  std::vector<fuchsia::element::Annotation> element_annotations;
  bool got_annotations{false};
  annotation_controller()->GetAnnotations(
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

  CreateStoryWithAnnotations({});

  // Set annotations.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  std::vector<fuchsia::element::Annotation> annotations_to_add;
  annotations_to_add.push_back(fidl::Clone(element_annotation));

  bool did_update_annotations{false};
  annotation_controller()->UpdateAnnotations(
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
  annotation_controller()->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        got_element_annotations = std::move(result.response().annotations);
        got_annotations = true;
      });

  RunLoopUntil([&]() { return got_annotations; });

  EXPECT_THAT(got_element_annotations,
              ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));
}

}  // namespace
}  // namespace modular
