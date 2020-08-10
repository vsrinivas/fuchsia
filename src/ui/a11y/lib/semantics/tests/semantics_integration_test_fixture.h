// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <memory>

#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {

using fuchsia::accessibility::semantics::Node;

// Test fixture that sets up an owned instance of semantics manager to run against.
//
// It publishes a SemanticsManager service in the test environment to allow components
// that use accessibility to connect to it. For components that are launched from the
// tests it provides methods to create a presentation and view holder token and method
// to retrieve the koid of a view launched by the test.
class SemanticsIntegrationTest : public sys::testing::TestWithEnvironment {
 public:
  SemanticsIntegrationTest(const std::string& environment_label);

  // |testing::Test|
  void SetUp() override;

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironment::CreateServices()|. In addition the default
  // implementation wires up SemanticsManager.
  virtual void CreateServices(std::unique_ptr<sys::testing::EnvironmentServices>& services) {}

  a11y::ViewManager* view_manager() { return &view_manager_; }
  sys::testing::EnclosingEnvironment* environment() { return environment_.get(); }

  fuchsia::ui::views::ViewToken CreatePresentationViewToken();

  // Recursively traverses the node hierarchy, rooted at |node|, to find the first descendant
  // with |label|.
  const Node* FindNodeWithLabel(const Node* node, zx_koid_t view_ref_koid, std::string label);

  // Get the transform between the view's local space and the node's local space.
  a11y::SemanticTransform GetTransformForNode(zx_koid_t view_ref_koid, uint32_t node_id);

  // Calculates the point in the view's local space corresponding to the point at
  // |node->location.min + offset| in the target node's local space.
  fuchsia::math::PointF CalculateViewTargetPoint(
      zx_koid_t view_ref_koid, const fuchsia::accessibility::semantics::Node* node,
      fuchsia::math::PointF offset);

  // Perform a hit test against the target node and return the node ID of the node (if any) that is
  // hit.
  std::optional<uint32_t> HitTest(zx_koid_t view_ref_koid, fuchsia::math::PointF target);

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

 private:
  const std::string environment_label_;
  std::unique_ptr<sys::ComponentContext> const component_context_;

  a11y::ViewManager view_manager_;
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> semantics_manager_bindings_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_SEMANTICS_INTEGRATION_TEST_FIXTURE_H_
