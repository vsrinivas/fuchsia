// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_GFX_ACCESSIBILITY_VIEW_H_
#define SRC_UI_A11Y_LIB_VIEW_GFX_ACCESSIBILITY_VIEW_H_

#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>
#include <optional>

#include "src/ui/a11y/lib/view/accessibility_view.h"

namespace a11y {

// Implements AccessibilityViewInterface using the GFX graphics composition API.
class GfxAccessibilityView : public AccessibilityViewInterface {
 public:
  explicit GfxAccessibilityView(sys::ComponentContext* component_context);
  ~GfxAccessibilityView() override = default;

  // Connects to scenic services, and inserts a11y view into the scene.
  // This method may be called more than once, so it resets the a11y view object
  // state.
  void Initialize();

  // |AccessibilityViewInterface|
  void add_view_properties_changed_callback(ViewPropertiesChangedCallback callback) override;

  std::optional<fuchsia::ui::gfx::ViewProperties> get_a11y_view_properties() {
    return a11y_view_properties_;
  }

  bool is_initialized() const {
    return proxy_view_holder_attached_ && proxy_view_connected_ &&
           proxy_view_holder_properties_set_;
  }

  // |AccessibilityViewInterface|
  std::optional<fuchsia::ui::views::ViewRef> view_ref() override;

  // |AccessibilityViewInterface|
  void add_scene_ready_callback(SceneReadyCallback callback) override;

  // |AccessibilityViewInterface|
  void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) override;

 private:
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events);

  // Component context, used to connect to scenic services.
  sys::ComponentContext* context_;

  // Interface between the accessibility view and the scenic service
  // that inserts it into the scene graph.
  fuchsia::ui::accessibility::view::RegistryPtr accessibility_view_registry_;

  // Scenic session interface.
  std::unique_ptr<scenic::Session> session_;

  // Scenic focuser used to request focus chain updates in the a11y view's subtree.
  fuchsia::ui::views::FocuserPtr focuser_;

  // Resources below must be declared after scenic session, because they
  // must be destroyed before the session is destroyed.

  // Holds the a11y view resource.
  // If not present, this view does not exist in the view tree.
  std::optional<scenic::View> a11y_view_;

  // Holds the "proxy" view holder. The proxy view sits between the a11y view
  // and client view(s) below. The purpose of this view is to enable the a11y
  // view to insert itself into the scene graph after the client view has
  // already been attached.
  // If not present, this view does not exist in the view tree.
  std::optional<scenic::ViewHolder> proxy_view_holder_;

  // Holds the a11y view properties.
  // If not present, the a11y view has not yet been connected to the scene.
  std::optional<fuchsia::ui::gfx::ViewProperties> a11y_view_properties_;

  // True if the Present() call that creates the proxy view holder and attaches it as a child of the
  // a11y view has completed.
  bool proxy_view_holder_attached_ = false;

  // True if the event that connects the proxy view to the client view was received.
  bool proxy_view_connected_ = false;

  // True if the Present() call that sets the proxy view holder's properties has
  // completed.
  bool proxy_view_holder_properties_set_ = false;

  // Holds a copy of the view ref of the a11y view.
  // If not present, the a11y view has not yet been connected to the scene.
  std::optional<fuchsia::ui::views::ViewRef> view_ref_;

  // If set, gets invoked whenever the view properties for the a11y view change.
  std::vector<ViewPropertiesChangedCallback> view_properties_changed_callbacks_;

  // If set, gets invoked when the scene becomes ready.
  std::vector<SceneReadyCallback> scene_ready_callbacks_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_GFX_ACCESSIBILITY_VIEW_H_
