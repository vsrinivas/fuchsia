// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_ACCESSIBILITY_VIEW_H_
#define SRC_UI_A11Y_LIB_VIEW_ACCESSIBILITY_VIEW_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace a11y {

// Interface for managing an accessibility view.
//
// This view is used to vend capabilities to the accessibility manager
// that a view confers, e.g. ability to request focus, consume and
// respond to input events, annotate underlying views, and apply
// coordinate transforms to its subtree.
class AccessibilityViewInterface {
 public:
  using ViewPropertiesChangedCallback =
      fit::function<bool(const fuchsia::ui::composition::ViewportProperties&)>;
  using SceneReadyCallback = fit::function<bool()>;
  using RequestFocusCallback = fit::function<void(fuchsia::ui::views::Focuser_RequestFocus_Result)>;

  AccessibilityViewInterface() = default;
  virtual ~AccessibilityViewInterface() = default;

  // Adds a callback to be invoked when the view properties for the a11y view change. When
  // registering this callback, if view properties are available this callback also gets invoked. If
  // the callback returns false when invoked, it no longer will receive future updates.
  virtual void add_view_properties_changed_callback(ViewPropertiesChangedCallback callback) = 0;

  // Adds a callback to be invoked when the scene is ready. If the callback returns false when
  // invoked, it no longer will receive future updates.
  virtual void add_scene_ready_callback(SceneReadyCallback callback) = 0;

  // Returns the view ref of the a11y view if the a11y view is ready.
  // If the a11y view is not yet ready, this method returns std::nullopt.
  virtual std::optional<fuchsia::ui::views::ViewRef> view_ref() = 0;

  // Attempts to transfer focus to the view corresponding to |view_ref|.
  virtual void RequestFocus(fuchsia::ui::views::ViewRef view_ref,
                            RequestFocusCallback callback) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_ACCESSIBILITY_VIEW_H_
