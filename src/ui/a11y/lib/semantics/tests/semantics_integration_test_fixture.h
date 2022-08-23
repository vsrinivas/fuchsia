// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_accessibility_view.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_injector_factory.h"
#include "src/ui/a11y/lib/view/view_manager.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace accessibility_test {

// Types imported for the realm_builder library.
using fuchsia::accessibility::semantics::Node;

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::Realm;

using RealmBuilder = component_testing::RealmBuilder;

// Mock component that will proxy SemanticsManager and SemanticTree requests to the ViewManager
// owned by the test fixture.
class SemanticsManagerProxy : public fuchsia::accessibility::semantics::SemanticsManager,
                              public LocalComponent {
 public:
  SemanticsManagerProxy(fuchsia::accessibility::semantics::SemanticsManager* semantics_manager,
                        async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), semantics_manager_(semantics_manager) {}
  ~SemanticsManagerProxy() override = default;

  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override;

  // |fuchsia::accessibility::semantics::SemanticsManager|
  void RegisterViewForSemantics(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_{};
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> bindings_;
  fuchsia::accessibility::semantics::SemanticsManager* semantics_manager_ = nullptr;
};

class SemanticsIntegrationTestV2
    : public gtest::RealLoopFixture,
      public testing::WithParamInterface<ui_testing::UITestRealm::Config> {
 public:
  static constexpr auto kSemanticsManager = "semantics_manager";
  static constexpr auto kSemanticsManagerRef = ChildRef{kSemanticsManager};

  SemanticsIntegrationTestV2() : context_(sys::ComponentContext::Create()) {}

  ~SemanticsIntegrationTestV2() override = default;

  void SetUp() override;

  virtual void ConfigureRealm() {}

  // Set of UI configurations to run our semantics integration tests against.
  static std::vector<ui_testing::UITestRealm::Config> UIConfigurationsToTest();

  // Returns the expected pixel scale factor observed by the client view with
  // the given `display_pixel_density`.
  static float ExpectedPixelScaleForDisplayPixelDensity(float display_pixel_density);

 protected:
  sys::ComponentContext* context() { return context_.get(); }
  a11y::ViewManager* view_manager() { return view_manager_.get(); }
  SemanticsManagerProxy* semantics_manager_proxy() { return semantics_manager_proxy_.get(); }
  Realm* realm() { return realm_.get(); }
  sys::ServiceDirectory* svc() { return realm_exposed_services_.get(); }
  zx_koid_t view_ref_koid() const { return view_ref_koid_; }

  // Initializes the scene, and waits for the client view to render.
  void SetupScene();

  // Recursively traverses the node hierarchy, rooted at |node|, to find the first descendant
  // with |label|.
  const Node* FindNodeWithLabel(const Node* node, zx_koid_t view_ref_koid, std::string label);

  // Get the transform between the view's local space and the node's local space.
  a11y::SemanticTransform GetTransformForNode(zx_koid_t view_ref_koid, uint32_t node_id);

  // Calculates the point in the view's local space corresponding to the point at the center of the
  // semantic node's bounding box.
  fuchsia::math::PointF CalculateCenterOfSemanticNodeBoundingBoxCoordinate(
      zx_koid_t view_ref_koid, const fuchsia::accessibility::semantics::Node* node);

  // Perform a hit test against the target node and return the node ID of the node (if any) that is
  // hit.
  std::optional<uint32_t> HitTest(zx_koid_t view_ref_koid, fuchsia::math::PointF target);

  // Perform an accessibility action against the target node and return whether or not the action
  // was handled
  bool PerformAccessibilityAction(zx_koid_t view_ref_koid, uint32_t node_id,
                                  fuchsia::accessibility::semantics::Action action);

  // Waits for the root semantic node's transform to include a scale of 1 /
  // expected_scale_factor.
  //
  // This scale factor is required for hit testing and scrolling, but may not be
  // present in the first committed version of the semantic tree. So, we should
  // gate spatial semantics tests on receipt of the correct scale factor.
  void WaitForScaleFactor();

 private:
  void BuildRealm();

  zx_koid_t view_ref_koid_;
  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<Realm> realm_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<a11y::ViewManager> view_manager_;
  std::unique_ptr<SemanticsManagerProxy> semantics_manager_proxy_;
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
