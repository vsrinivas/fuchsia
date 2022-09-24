// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

namespace a11y {

// Entity that keeps track of a11y focus per semantic-providing view.
//
// The a11y focus is defined as the semantic node which is selected in a certain
// view by the screen reader. There is only one active a11y focus, meaning that
// the screen reader cares only about one node per time. If the system changes
// the Focus Chain to a different view, the a11y focus also changes.
//
// The a11y focus can be changed, which may trigger a Focus Chain Update if the
// active a11y focus is moving to another view.
class A11yFocusManager {
 public:
  // Defines which view is currently in a11y focus along with the node_id of the node inside that
  // view.
  struct A11yFocusInfo {
    zx_koid_t view_ref_koid;
    uint32_t node_id;
  };

  // Callback which will be used to notify that an error is encountered while trying to set a11y
  // focus.
  using SetA11yFocusCallback = fit::function<void(bool)>;

  // Callback used to inform when the a11y focus changes.
  using OnA11yFocusUpdatedCallback = fit::function<void(std::optional<A11yFocusInfo>)>;

  virtual ~A11yFocusManager() = default;

  // Returns the current a11y focus, if any.
  virtual std::optional<A11yFocusInfo> GetA11yFocus() = 0;

  // Sets the a11y focus.
  //
  // If the new focus is in a different view from the current focus, then the
  // focus manager will request a focus chain update from scenic, unless:
  //
  // (1) The new view does not provide semantics.
  // (2) The new view contains a visible virtual keyboard.
  virtual void SetA11yFocus(zx_koid_t koid, uint32_t node_id, SetA11yFocusCallback callback) = 0;

  // Clears existing a11y focus.
  virtual void ClearA11yFocus() = 0;

  // Removes current highlights (if any), and highlights the node specified by (newly_focused_view,
  // newly_focused_node).
  virtual void UpdateHighlights(zx_koid_t newly_focused_view, uint32_t newly_focused_node) = 0;

  // Registers a callback that is invoked when the a11y focus is updated.
  virtual void set_on_a11y_focus_updated_callback(
      OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback) = 0;
};

}  //  namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
