// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

namespace a11y {

// The A11yFocusManager keeps track of a11y focus and a cache of the "last
// focused node" for each view.
//
// The a11y focus is defined as the semantic node which is selected in a certain
// view by the screen reader. There is only (up to) one active a11y focus, meaning that
// the screen reader cares only about (up to) one node at a time.
//
// The view in a11y focus and the view in input focus are almost always kept in sync:
// - If the system changes the Focus Chain to a different view, the a11y focus
// also follows. (If a node was previously focused in that view, it regains
// focus; otherwise the a11y focus is lost.)
// - If the a11y focus is changed, this will trigger a Focus Chain Update if the
// active a11y focus is moving to another view (except in rare situations
// involving the virtual keyboard).
//
// (For clarity, the following terms are equivalent: 'view in input focus', 'view in Scenic focus',
// and 'view at the end of the focus chain'.)
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

  // Try to restore the a11y focus to the view in input focus, if possible.
  // After this method completes, GetA11yFocus() will generally return a non-nullopt
  // value (except in some rare situations).
  virtual void RestoreA11yFocusToInputFocus() = 0;

  // Tries to set the a11y focus.
  //
  // If the new focus is in a different view from the current input focus, then
  // we'll send a focus chain update request to scenic -- unless the new view
  // contains a visible virtual keyboard.
  //
  // If the scenic focus chain update succeeds (or was eschewed), we'll
  // (1) set the a11y focus to {koid, node_id}, (2) redraw highlights, and (3)
  // call the callback with 'true'.
  // Otherwise, we'll call the callback with 'false'.
  virtual void SetA11yFocus(zx_koid_t koid, uint32_t node_id, SetA11yFocusCallback callback) = 0;

  // Clears existing a11y focus, and forgets the current view's "last focused node".
  virtual void ClearA11yFocus() = 0;

  // If a node is in a11y focus, redraws the current highlights (useful if the
  // node's bounding box has changed).
  // Otherwise, clears any highlights.
  virtual void RedrawHighlights() = 0;

  // Registers a callback that is invoked when the a11y focus is updated. For now, only one callback
  // can be registered at a time.
  virtual void set_on_a11y_focus_updated_callback(
      OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback) = 0;
};

}  //  namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
