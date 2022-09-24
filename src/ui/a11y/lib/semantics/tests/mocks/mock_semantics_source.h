// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <map>
#include <optional>
#include <queue>

#include "src/ui/a11y/lib/semantics/semantics_source.h"
#include "src/ui/a11y/lib/semantics/typedefs.h"

namespace accessibility_test {

class MockSemanticsSource : public a11y::SemanticsSource {
 public:
  MockSemanticsSource() = default;
  ~MockSemanticsSource() = default;

  // Adds a ViewRef to be owned by this mock. Calls to ViewHasSemantics() and ViewRefClone() will
  // respond to this ViewRef accordingly.
  void AddViewRef(fuchsia::ui::views::ViewRef view_ref);

  // Sets return value for ViewHasSemantics().
  void set_view_has_semantics(bool view_has_semantics) { view_has_semantics_ = view_has_semantics; }

  // |SemanticsSource|
  bool ViewHasSemantics(zx_koid_t view_ref_koid) override;

  // Sets perform accessibility action return value.
  void set_perform_accessibility_action_callback_value(bool value) {
    perform_accessibility_action_callback_value_ = value;
  }

  // Sets a callback to mock action handling. This callback will be invoked in
  // PerformAccessibilityAction().
  void set_custom_action_callback(fit::function<void()> callback) {
    custom_action_callback_ = std::move(callback);
  }

  // |SemanticsSource|
  std::optional<fuchsia::ui::views::ViewRef> ViewRefClone(zx_koid_t view_ref_koid) override;

  // Creates a semantic node that can be retrieved using |GetSemanticNode()|.
  void CreateSemanticNode(zx_koid_t koid, fuchsia::accessibility::semantics::Node node);

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetSemanticNode(zx_koid_t koid,
                                                                 uint32_t node_id) const override;
  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetNextNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetNextNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetParentNode(zx_koid_t koid,
                                                               uint32_t node_id) const override;
  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const override;

  // |SemanticsSource|
  const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const override;

  // Sets result of hit test on view corresponding to |koid|.
  void SetHitTestResult(zx_koid_t koid, fuchsia::accessibility::semantics::Hit hit_test_result);

  // |SemanticsSource|
  void ExecuteHitTesting(
      zx_koid_t koid, fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) override;

  // Sets the result of an action in view corresponding to |koid|.
  void SetActionResult(zx_koid_t koid, bool action_result);

  // |SemanticsSource|
  void PerformAccessibilityAction(
      zx_koid_t koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback) override;

  // Set the SemanticTransform for GetNodeToRootTransform() to return.
  void SetNodeToRootTransform(a11y::SemanticTransform semantic_transform);

  // |SemanticsSource|
  std::optional<a11y::SemanticTransform> GetNodeToRootTransform(zx_koid_t koid,
                                                                uint32_t node_id) const override;

  // Returns list of actions requested on view corresponding to |koid|, in the order they were
  // requested.
  const std::vector<std::pair<uint32_t, fuchsia::accessibility::semantics::Action>>&
  GetRequestedActionsForView(zx_koid_t koid);

  // Methods to mark GetNextNode(), GetParentNode(), and GetPreviousNode() for
  // failure, respectively.
  void set_get_next_node_should_fail(bool should_fail) { get_next_node_should_fail_ = should_fail; }
  void set_get_parent_node_should_fail(bool should_fail) {
    get_parent_node_should_fail_ = should_fail;
  }
  void set_get_previous_node_should_fail(bool should_fail) {
    get_previous_node_should_fail_ = should_fail;
  }

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  // Map of koid to hit test result for corresponding view.
  std::map<zx_koid_t, fuchsia::accessibility::semantics::Hit> hit_test_results_;

  // Map of koid to (node_id, node) map for each view.
  std::map<zx_koid_t, std::map<uint32_t, fuchsia::accessibility::semantics::Node>> nodes_;

  // Map of koid to actions requested in corresponding view (in order requests were received).
  std::map<zx_koid_t, std::vector<std::pair<uint32_t, fuchsia::accessibility::semantics::Action>>>
      requested_actions_;

  // Map of koid to return value for actions requested in corresponding view.
  std::map<zx_koid_t, bool> action_results_;

  // Semantic transform to be returned by GetNodeToRootTransform().
  std::optional<a11y::SemanticTransform> transform_to_return_ = std::nullopt;

  // Value that will be passed to the PerformAccessibilityAction callback.
  // Default to true since we want this method to return "success" by default.
  bool perform_accessibility_action_callback_value_ = true;

  // Callback invoked in PerformAccessibilityAction. This callback allows users
  // to supply a custom action handler.
  fit::function<void()> custom_action_callback_ = {};

  // Indicates whether the corresponding method should return null.
  bool get_next_node_should_fail_ = false;
  bool get_parent_node_should_fail_ = false;
  bool get_previous_node_should_fail_ = false;

  // Return value for ViewHasSemantics.
  // Default to true since it's the more common testing use case.
  bool view_has_semantics_ = true;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_
