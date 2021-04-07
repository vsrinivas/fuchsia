// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/annotations.h"

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/fit/function.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_element.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_graphical_presenter.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr auto kTestAnnotationKey = "test_annotation_key";
constexpr auto kTestAnnotationValue = "test_annotation_value";

using element::annotations::AnnotationEq;
using ::testing::ByRef;
using ::testing::ElementsAre;

class AnnotationsTest : public modular_testing::TestHarnessFixture {
 protected:
  AnnotationsTest()
      : fake_graphical_presenter_(
            modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions()),
        element_(modular_testing::FakeElement::CreateWithDefaultOptions()) {}

  fuchsia::element::ManagerPtr& element_manager() { return element_manager_; }
  fuchsia::element::AnnotationControllerPtr& annotation_controller() {
    return annotation_controller_;
  }
  modular_testing::FakeGraphicalPresenter* graphical_presenter() {
    return fake_graphical_presenter_.get();
  }
  modular_testing::FakeElement* element() { return element_.get(); }

  void StartSession() {
    modular_testing::TestHarnessBuilder builder;
    builder.InterceptSessionShell(fake_graphical_presenter_->BuildInterceptOptions());
    builder.InterceptComponent(element_->BuildInterceptOptions());

    bool graphical_presenter_connected = false;
    fake_graphical_presenter_->set_on_graphical_presenter_connected([&]() {
      graphical_presenter_connected = true;
      fake_graphical_presenter_->set_on_graphical_presenter_error([&](zx_status_t status) {});
    });
    fake_graphical_presenter_->set_on_graphical_presenter_error([&](zx_status_t status) {
      FX_PLOGS(FATAL, status) << "Failed to connect to FakeGraphicalPresenter";
      FX_NOTREACHED();
    });

    builder.BuildAndRun(test_harness());

    fuchsia::modular::testing::ModularService request;
    request.set_element_manager(element_manager_.NewRequest());
    test_harness()->ConnectToModularService(std::move(request));

    EXPECT_FALSE(fake_graphical_presenter_->is_running());
    RunLoopUntil([&] { return fake_graphical_presenter_->is_running(); });
    RunLoopUntil([&] { return graphical_presenter_connected; });

    // Add Event Listeners
    graphical_presenter()->set_on_present_view(
        [&](fuchsia::element::ViewSpec view_spec,
            fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
          annotation_controller_ = annotation_controller.Bind();
        });
  }

  void CheckElementControllerAnnotations(
      fuchsia::element::ControllerPtr& element_controller,
      fit::function<void(std::vector<fuchsia::element::Annotation>&)> check_annotations) {
    bool checked_annotation{false};
    element_controller->GetAnnotations(
        [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
          ASSERT_FALSE(result.is_err());
          check_annotations(result.response().annotations);
          checked_annotation = true;
        });

    RunLoopUntil([&] { return checked_annotation; });
  }

  void CheckAnnotationControllerAnnotations(
      fit::function<void(std::vector<fuchsia::element::Annotation>&)> check_annotations) {
    bool checked_annotation{false};
    annotation_controller()->GetAnnotations(
        [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
          ASSERT_FALSE(result.is_err());
          check_annotations(result.response().annotations);
          checked_annotation = true;
        });

    RunLoopUntil([&] { return checked_annotation; });
  }

 private:
  fuchsia::element::ManagerPtr element_manager_;
  fuchsia::element::AnnotationControllerPtr annotation_controller_;
  std::unique_ptr<modular_testing::FakeGraphicalPresenter> fake_graphical_presenter_;
  std::unique_ptr<modular_testing::FakeElement> element_;
};

// Tests that updates to an element's annotations using the element's controller are reflected
// in an AnnotationController associated with the same story.
TEST_F(AnnotationsTest, UpdateAnnotationsThroughElementController) {
  StartSession();

  // Propose an element without annotations.
  bool is_proposed{false};
  fuchsia::element::ControllerPtr element_controller;
  element_manager()->ProposeElement(fidl::Clone(element()->spec()), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed && element()->is_running(); });

  // Check that the element has no annotations.
  fuchsia::element::Annotation annotation_to_check;
  CheckElementControllerAnnotations(
      element_controller, [](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });
  CheckAnnotationControllerAnnotations(
      [](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });

  // Update the element's annotations through the element's controller.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
  std::vector<fuchsia::element::Annotation> annotations_to_set;
  annotations_to_set.push_back(fidl::Clone(element_annotation));

  bool did_update_annotations{false};
  element_controller->UpdateAnnotations(
      std::move(annotations_to_set),
      /*annotations_to_delete=*/{},
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });
  RunLoopUntil([&] { return did_update_annotations; });

  // Assert the annotation controller reflects updated annotations.
  CheckAnnotationControllerAnnotations(
      [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });
}

// Tests that updates to an element's annotations using an AnnotationController are reflected
// in the element's controller.
TEST_F(AnnotationsTest, UpdateAnnotationsThroughAnnotationController) {
  StartSession();

  // Propose an element without annotations.
  bool is_proposed{false};
  fuchsia::element::ControllerPtr element_controller;
  element_manager()->ProposeElement(fidl::Clone(element()->spec()), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed && element()->is_running(); });

  // Check that the element has no annotations.
  fuchsia::element::Annotation annotation_to_check;
  CheckElementControllerAnnotations(
      element_controller, [](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });
  CheckAnnotationControllerAnnotations(
      [](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });

  // Update the element's annotations through the AnnotationController.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
  std::vector<fuchsia::element::Annotation> annotations_to_set;
  annotations_to_set.push_back(fidl::Clone(element_annotation));

  bool did_update_annotations{false};
  annotation_controller()->UpdateAnnotations(
      std::move(annotations_to_set),
      /*annotations_to_delete=*/{},
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });
  RunLoopUntil([&] { return did_update_annotations; });

  // Assert the element controller reflects updated annotations.
  CheckElementControllerAnnotations(
      element_controller, [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });
}

// Tests that deleting an element's annotation using the element's controller is reflected
// in an AnnotationController associated with the same element.
TEST_F(AnnotationsTest, DeleteAnnotationsThroughElementController) {
  StartSession();

  // Create an ElementSpec with an annotation.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
  auto element_spec = fidl::Clone(element()->spec());
  element_spec.mutable_annotations()->push_back(fidl::Clone(element_annotation));

  // Propose the element
  bool is_proposed{false};
  fuchsia::element::ControllerPtr element_controller;
  element_manager()->ProposeElement(std::move(element_spec), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed && element()->is_running(); });

  // Check that the element has annotations.
  CheckElementControllerAnnotations(
      element_controller, [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });
  CheckAnnotationControllerAnnotations(
      [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });

  // Delete the element's annotation through the element's controller.
  auto key_to_delete = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey);
  std::vector<fuchsia::element::AnnotationKey> annotations_to_delete;
  annotations_to_delete.push_back(std::move(key_to_delete));

  bool did_update_annotations{false};
  element_controller->UpdateAnnotations(
      /*annotations_to_set=*/{}, std::move(annotations_to_delete),
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });
  RunLoopUntil([&] { return did_update_annotations; });

  // Assert the annotation controller reflects updated annotations.
  CheckAnnotationControllerAnnotations(
      [](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });
}

// Tests that deleting an element's annotation using an AnnotationController is reflected
// in an AnnotationController associated with the same element.
TEST_F(AnnotationsTest, DeleteAnnotationsThroughAnnotationController) {
  StartSession();

  // Create an ElementSpec with an annotation.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
  auto element_spec = fidl::Clone(element()->spec());
  element_spec.mutable_annotations()->push_back(fidl::Clone(element_annotation));

  // Propose the element
  bool is_proposed{false};
  fuchsia::element::ControllerPtr element_controller;
  element_manager()->ProposeElement(std::move(element_spec), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed && element()->is_running(); });

  // Check that the element has annotations.
  CheckElementControllerAnnotations(
      element_controller, [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });
  CheckAnnotationControllerAnnotations(
      [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(element_annotation))));
      });

  // Delete the element's annotation through the AnnotationController.
  auto key_to_delete = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey);
  std::vector<fuchsia::element::AnnotationKey> annotations_to_delete;
  annotations_to_delete.push_back(std::move(key_to_delete));

  bool did_update_annotations{false};
  annotation_controller()->UpdateAnnotations(
      /*annotations_to_set=*/{}, std::move(annotations_to_delete),
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });
  RunLoopUntil([&] { return did_update_annotations; });

  // Assert the element controller reflects updated annotations.
  CheckElementControllerAnnotations(
      element_controller, [&](std::vector<fuchsia::element::Annotation>& annotations_to_check) {
        EXPECT_TRUE(annotations_to_check.empty());
      });
}

}  // namespace
