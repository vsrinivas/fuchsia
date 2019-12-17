// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace accessibility_test {

class MockSemanticListener : public fuchsia::accessibility::semantics::SemanticListener {
 public:
  MockSemanticListener() = default;

  ~MockSemanticListener() override = default;

  // |fuchsia::accessibility::semantics::SemanticListener|
  void OnAccessibilityActionRequested(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback) override;

  // |fuchsia::accessibility::semantics::SemanticListener|
  void HitTest(::fuchsia::math::PointF local_point, HitTestCallback callback) override;

  // |fuchsia::accessibility::semantics::SemanticListener|
  void OnSemanticsModeChanged(bool update_enabled,
                              OnSemanticsModeChangedCallback callback) override;

  // Sets hit_test_node_id_ with given node_id, which will then be returned when
  // HitTest() is called.
  void SetHitTestResult(int node_id);

  // Sets is_accessibility_action_requested_called_ with the given boolean value. This will be used
  // to track if OnAccessibilityActionRequested() is called.
  void SetIsAccessibilityActionRequestedCalled(bool is_called);

  // Returns is_accessibility_action_requested_called_ flag.
  bool GetIsAccessibilityActionRequestedCalled() const;

  // Sets receive_action_ with the given action.
  void SetRequestedAction(fuchsia::accessibility::semantics::Action action);

  // Returns receive_action_ with the given action. This will be used to track if
  // OnAccessibilityActionRequested() is called with correct action.
  fuchsia::accessibility::semantics::Action GetRequestedAction() const;

  void Bind(fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> *listener);

  bool GetSemanticsEnabled() { return semantics_enabled_; }

 private:
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticListener> semantic_listener_bindings_;

  // Node id which will be returned when HitTest() is called.
  uint32_t hit_test_node_id_ = 1;

  bool semantics_enabled_ = false;

  fuchsia::accessibility::semantics::Action received_action_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticListener);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_LISTENER_H_
