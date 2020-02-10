// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace accessibility_test {
using fuchsia::accessibility::semantics::Action;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::SemanticListener;
using fuchsia::accessibility::semantics::SemanticsManagerPtr;
using fuchsia::accessibility::semantics::SemanticTreePtr;

class MockSemanticListener : public SemanticListener {
 public:
  // Mock for SemanticProvider and Semantic Action Listener, which will be
  // responsible for sending Node updates to A11y Manager and handling
  // OnAccessibillityActionRequested() requests.
  //
  // On initialization, MockSemanticListener tries to connect to
  // |fuchsia::accessibility::ViewManager| service in |context_| and
  // registers with it's view_ref, binding and interface request.
  explicit MockSemanticListener(sys::testing::ComponentContextProvider* context_provider,
                                fuchsia::ui::views::ViewRef view_ref);
  ~MockSemanticListener() override = default;

  void UpdateSemanticNodes(std::vector<Node> nodes);
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids);
  void CommitUpdates();

 private:
  // |fuchsia::accessibility::semantics::SemanticListener|
  void OnAccessibilityActionRequested(
      uint32_t node_id, Action action,
      SemanticListener::OnAccessibilityActionRequestedCallback callback) override {}

  // |fuchsia::accessibility::semantics::SemanticListener|
  void HitTest(::fuchsia::math::PointF local_point, HitTestCallback callback) override {}

  // |fuchsia::accessibility::semantics::SemanticListener|
  void OnSemanticsModeChanged(bool enabled, OnSemanticsModeChangedCallback callback) override {}

  sys::testing::ComponentContextProvider* context_provider_;
  SemanticsManagerPtr manager_;
  SemanticTreePtr tree_ptr_;
  fidl::BindingSet<SemanticListener> bindings_;
  fuchsia::ui::views::ViewRef view_ref_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticListener);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_
