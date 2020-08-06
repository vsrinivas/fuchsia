// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <cstdint>
#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

// Mocks Semantics Provider(implemented by Flutter/Chrome) which is responsible for providing
// semantic tree to Semantics Manager.
class MockSemanticProvider {
 public:
  // On initialization, MockSemanticProvider tries to connect to
  // |fuchsia::accessibility::ViewManager| service in |manager| and
  // registers with it's view_ref, binding and interface request.
  explicit MockSemanticProvider(fuchsia::accessibility::semantics::SemanticsManager* manager);
  ~MockSemanticProvider() = default;

  zx_koid_t koid() const { return a11y::GetKoid(view_ref_); };

  const fuchsia::ui::views::ViewRef& view_ref() { return view_ref_; }

  // Calls UpdateSemanticNodes() on SemanticTree with given nodes list.
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes);

  // Calls DeleteSemanticNodes() on SemanticTree with given nodes list.
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids);

  // Calls Commit() Updates.
  void CommitUpdates();

  // Sets hit_test_result in MockSemanticListener. If no value is passed, the hit test will return
  // an empty hit test. Used to simulate errors.
  void SetHitTestResult(std::optional<uint32_t> hit_test_result);

  // Returns Commit Failed status.
  bool CommitFailedStatus() const { return commit_failed_; };

  void SetSemanticsEnabled(bool enabled);

  // Returns Semantics Enabled field from Semantic Listener.
  bool GetSemanticsEnabled();

  // Sets receive_action_ with the given action.
  void SetRequestedAction(fuchsia::accessibility::semantics::Action action);

  // Returns receive_action_ with the given action. This will be used to track if
  // OnAccessibilityActionRequested() is called with correct action.
  fuchsia::accessibility::semantics::Action GetRequestedAction() const;

  // Returns node_id on which action is called.
  uint32_t GetRequestedActionNodeId() const;

  // Function for sending signal to the view ref peer.
  void SendEventPairSignal();

  fuchsia::ui::views::ViewRef CreateOrphanViewRef();

  // Returns true if channel is closed.
  bool IsChannelClosed();

  // Sets |slider_delta_| which is used to increment or decrement slider range_value.
  void SetSliderDelta(uint32_t slider_delta);

  // Set slider node which is used to update semantic tree when Increment or Decrement action is
  // called.
  void SetSliderNode(fuchsia::accessibility::semantics::Node new_node);

  // Sets the status of OnAccessibilityActionRequestedCallback.
  void SetOnAccessibilityActionCallbackStatus(bool status);

  // Returns true if a call to OnAccessibilityActionRequested() is made.
  bool OnAccessibilityActionRequestedCalled() const;

 private:
  // Pointer to semantic tree which is used for sending Update/Delete/Commit
  // messages.
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;

  // Value by which the slider is incremented or decremented.
  uint32_t slider_delta_ = 1;

  // Slider node used for updating semantic tree when Increment or Decrement action is called.
  fuchsia::accessibility::semantics::Node slider_node_;

  // ViewRef of the Semantic Tree.
  zx::eventpair eventpair_peer_;
  fuchsia::ui::views::ViewRef view_ref_;

  bool commit_failed_;
  MockSemanticListener semantic_listener_;
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticListener> semantic_listener_bindings_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticProvider);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
