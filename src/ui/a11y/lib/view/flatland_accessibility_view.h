// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_FLATLAND_ACCESSIBILITY_VIEW_H_
#define SRC_UI_A11Y_LIB_VIEW_FLATLAND_ACCESSIBILITY_VIEW_H_

#include <fuchsia/accessibility/scene/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>
#include <optional>

#include "src/ui/a11y/lib/annotation/highlight_delegate.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"
#include "src/ui/a11y/lib/view/accessibility_view.h"
#include "src/ui/a11y/lib/view/flatland_connection.h"

namespace a11y {

// Implements the AccessibilityViewInterface using the flatland graphics
// composition API.
class FlatlandAccessibilityView : public AccessibilityViewInterface,
                                  public fuchsia::accessibility::scene::Provider,
                                  public HighlightDelegate,
                                  public Magnifier2::Delegate {
 public:
  explicit FlatlandAccessibilityView(fuchsia::ui::composition::FlatlandPtr flatland1,
                                     fuchsia::ui::composition::FlatlandPtr flatland2);
  ~FlatlandAccessibilityView() override = default;

  // |AccessibilityViewInterface|
  void add_view_properties_changed_callback(ViewPropertiesChangedCallback callback) override;

  // |AccessibilityViewInterface|
  std::optional<fuchsia::ui::views::ViewRef> view_ref() override;

  // |AccessibilityViewInterface|
  void add_scene_ready_callback(SceneReadyCallback callback) override;

  // |AccessibilityViewInterface|
  void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) override;

  // |fuchsia::accessibility::scene::Provider|
  void CreateView(fuchsia::ui::views::ViewCreationToken a11y_view_token,
                  fuchsia::ui::views::ViewportCreationToken proxy_viewport_token) override;

  // |HighlightDelegate|
  void DrawHighlight(fuchsia::math::Point top_left, fuchsia::math::Point bottom_right,
                     fit::function<void()> callback) override;

  // |HighlightDelegate|
  void ClearHighlight(fit::function<void()> callback) override;

  // |Magnifier2::Delegate|
  void SetMagnificationTransform(float scale, float x, float y,
                                 SetMagnificationTransformCallback callback) override;

  fidl::InterfaceRequestHandler<fuchsia::accessibility::scene::Provider> GetHandler();

 private:
  // Helper method to poll continuously for layout info updates.
  void WatchForResizes();

  // Helper method to handle layout changes.
  void ResizeLayout(fuchsia::math::SizeU logical_size);

  // Manages a11y view's flatland connection.
  FlatlandConnection flatland_a11y_;

  // Manages highlight view's flatland connection.
  FlatlandConnection flatland_highlight_;

  // Scenic focuser used to request focus chain updates in the a11y view's subtree.
  fuchsia::ui::views::FocuserPtr focuser_;

  // Used to retrieve a11y view layout info.
  fuchsia::ui::composition::ParentViewportWatcherPtr parent_watcher_;

  // True if we've received a CreateView request.
  bool received_create_view_request_ = false;

  // True if the a11y view and highlight views have been attached to the scene.
  bool is_initialized_ = false;

  // True iff DrawHighlight() has been called more recently than ClearHighlight().
  // Also true iff the transform w/ id kHighlightTransformId is currently a child
  // of the transform w/ id kHighlightViewRootTransformId.
  bool highlight_is_present_ = false;

  // Holds the proxy viewport creation token between the time that `CreateView`
  // is called, and the first layout info is received from scenic.
  //
  // Otherwise, proxy_viewport_token_ will be std::nullopt.
  std::optional<fuchsia::ui::views::ViewportCreationToken> proxy_viewport_token_;

  // Holds a copy of the view ref of the a11y view.
  // If not present, the a11y view has not yet been connected to the scene.
  std::optional<fuchsia::ui::views::ViewRef> a11y_view_ref_;

  // Layout info for the a11y view. If std::nullopt, then layout info has not yet
  // been received.
  std::optional<fuchsia::ui::composition::LayoutInfo> layout_info_;

  // If set, gets invoked whenever the view properties for the a11y view change.
  std::vector<ViewPropertiesChangedCallback> view_properties_changed_callbacks_;

  // If set, gets invoked when the scene becomes ready.
  std::vector<SceneReadyCallback> scene_ready_callbacks_;

  fidl::BindingSet<fuchsia::accessibility::scene::Provider> view_bindings_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_FLATLAND_ACCESSIBILITY_VIEW_H_
