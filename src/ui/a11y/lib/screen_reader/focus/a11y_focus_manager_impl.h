// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_IMPL_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_IMPL_H_

#include <lib/async/cpp/wait.h>
#include <lib/inspect/cpp/inspect.h>

#include <unordered_map>

#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_listener.h"
#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/view/view_source.h"
#include "src/ui/a11y/lib/virtual_keyboard/virtual_keyboard_manager.h"

namespace a11y {

// The A11yFocusManagerImpl keeps track of a11y focus, including a cache of the last focused node
// for each view.
//
// The a11y focus is defined as the semantic node which is selected in a certain
// view by the screen reader. There is only (up to) one active a11y focus, meaning that
// the screen reader cares only about (up to) one node per time.
//
// If the system changes the Focus Chain to a different view, the a11y focus
// also changes: If a node was previously focused in that view, it
// regains focus, otherwise the a11y focus will be lost.
//
// The a11y focus can be changed, which may trigger a Focus Chain Update if the active a11y focus is
// moving to another view.
class A11yFocusManagerImpl : public A11yFocusManager, public AccessibilityFocusChainListener {
 public:
  // Root node id, which will be used to set the default node_id for a view.
  static constexpr uint32_t kRootNodeId = 0;

  static constexpr char kCurrentlyFocusedKoidInspectNodeName[] = "currently_focused_koid";
  static constexpr char kCurrentlyFocusedNodeIdInspectNodeName[] = "currently_focused_node_id";

  // |focus_chain_requester| and |registry| must outlive this object.
  explicit A11yFocusManagerImpl(AccessibilityFocusChainRequester* focus_chain_requester,
                                AccessibilityFocusChainRegistry* registry, ViewSource* view_source,
                                VirtualKeyboardManager* virtual_keyboard_manager,
                                inspect::Node inspect_node = inspect::Node());
  ~A11yFocusManagerImpl() override;

  // |A11yFocusManager|
  //
  // Returns the current a11y focus, if any.
  std::optional<A11yFocusInfo> GetA11yFocus() override;

  // |A11yFocusManager|
  //
  // Sets the a11y focus.
  //
  // If the new focus is in a different view from the current focus, then
  // this manager will send a focus chain update request to scenic -- unless the
  // new view contains a visible virtual keyboard.
  //
  // If the scenic focus chain update either succeeds or was eschewed, we set
  // the a11y focus to {koid, node_id} and call the callback with 'true'.
  // Otherwise, we call the callback with 'false'.
  void SetA11yFocus(zx_koid_t koid, uint32_t node_id, SetA11yFocusCallback callback) override;

  // |A11yFocusManager|
  //
  // Clears existing a11y focus.
  void ClearA11yFocus() override;

  // |A11yFocusManager|
  //
  // Removes current highlights (if any), and highlights the node specified by (newly_focused_view,
  // newly_focused_node).
  void UpdateHighlights(zx_koid_t newly_focused_view, uint32_t newly_focused_node) override;

  // |A11yFocusManager|
  //
  // Registers a callback that is invoked when the a11y focus is updated. For now, only one callback
  // can be registered at a time.
  void set_on_a11y_focus_updated_callback(
      OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback) override {
    on_a11y_focus_updated_callback_ = std::move(on_a11y_focus_updated_callback);
  }

 private:
  // |AccessibilityFocusChainListener|
  void OnViewFocus(zx_koid_t view_ref_koid) override;

  // Helper function to update inspect info.
  void UpdateInspectProperties();

  // Helper method to update highlights, focus bookkeeping, and inspect, and to
  // invoke the on focus callback.
  //
  // This set of operations must occur in the same order on every focus change,
  // regardless of whether the focus change occurs within the same view.
  void UpdateFocus(zx_koid_t newly_focused_view, uint32_t newly_focused_node);

  // Removes current highlights (if any).
  void ClearHighlights();

  // Map for storing node_id which is in a11y focus for every viewref_koid.
  // By default, root-node(node_id = 0) is set for a view in a11y focus.
  std::unordered_map<zx_koid_t /* viewref_koid */, uint32_t /* node_id */>
      focused_node_in_view_map_;

  // Stores the koid of the view which is currently in a11y focus.
  zx_koid_t currently_focused_view_ = ZX_KOID_INVALID;

  // Interface used to request Focus Chain Updates.
  AccessibilityFocusChainRequester* const focus_chain_requester_ = nullptr;

  // Used to retrieve semantic tree data and manipulate highlights.
  ViewSource* const view_source_ = nullptr;

  // Used to retrieve information about visible virtual keyboards.
  VirtualKeyboardManager* const virtual_keyboard_manager_ = nullptr;

  OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback_;

  fxl::WeakPtrFactory<AccessibilityFocusChainListener> weak_ptr_factory_;

  // Inspect node to which to publish debug info.
  inspect::Node inspect_node_;

  // Inspect properties to store current a11y focus.
  inspect::UintProperty inspect_property_current_focus_koid_;
  inspect::UintProperty inspect_property_current_focus_node_id_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_IMPL_H_
