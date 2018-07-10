// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TALKBACK_TALKBACK_IMPL_H_
#define GARNET_BIN_A11Y_TALKBACK_TALKBACK_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/tts/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace talkback {

// Talkback action functions. The gesture recognizer calls these functions
// once the right gestures are applied.
// The functionality we try to expose here include:
// - Single tap/touch explore on a UI element to read aloud element/set
//   accessibility focus on it.
// - Double tap to select the current element with accessibility focus.
// Only the functionality that needs to be mediated by the accessibility
// manager is performed here. Talkback also allows for using two fingers
// to simulate one finger, but that is handled only in the gesture recognizer.
class TalkbackImpl {
 public:
  explicit TalkbackImpl(component::StartupContext* startup_context);
  ~TalkbackImpl() = default;

  // Should be called on a single tap gesture or when a finger is moving on the
  // screen for touch exploration. Queries the a11y manager semantics tree to
  // find the semantics node that the pointer event coordinates hits. Once
  // found, asks the a11y manager to set accessibility focus on the returned
  // node. Takes in |token| and |event| as arguments needed to perform
  // hit-testing in the a11y manager.
  void SetAccessibilityFocus(fuchsia::ui::viewsv1::ViewTreeToken token,
                             fuchsia::ui::input::PointerEvent event);
  // Asks the a11y manager to apply an a11y tap action to the current
  // accessibility focused node. Accessibility focus should first be set
  // before calling this function.
  void TapAccessibilityFocusedNode();

 private:
  // Listener function for node change events sent by the a11y manager.
  // We specifically check if the node has gained accessibility focus,
  // we read aloud the node label using the connected TTS service.
  void OnNodeAction(int32_t view_id, fuchsia::accessibility::Node node,
                    fuchsia::accessibility::Action action);
  // Helper function for SetAccessibilityFocus once a11y manager returns the
  // found node after hit-testing. No-ops if the returned node is already
  // currently focused.
  void OnHitAccessibilityNodeCallback(int32_t view_id,
                                      fuchsia::accessibility::NodePtr node_ptr);

  fuchsia::accessibility::ManagerPtr manager_;
  fuchsia::tts::TtsServicePtr tts_;

  // View id of the current a11y focused node.
  int32_t focused_view_id_ = -1;
  // Local node id of the current a11y focused node.
  int32_t focused_node_id_ = -1;

  FXL_DISALLOW_COPY_AND_ASSIGN(TalkbackImpl);
};

}  // namespace talkback

#endif  // GARNET_BIN_A11Y_TALKBACK_TALKBACK_IMPL_H_
