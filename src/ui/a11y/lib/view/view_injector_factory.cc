// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_injector_factory.h"

namespace a11y {

std::shared_ptr<input::Injector> ViewInjectorFactory::BuildAndConfigureInjector(
    AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
    fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
  auto injector =
      std::make_shared<input::Injector>(component_context, std::move(context), std::move(target));
  std::weak_ptr<input::Injector> injector_weak_ptr = injector;
  a11y_view->add_scene_ready_callback([injector = injector_weak_ptr]() {
    auto ptr = injector.lock();
    if (!ptr) {
      return false;
    }
    // Note: a11y uses a device id == 1 here since only one touch screen device is supported at the
    // moment.
    ptr->OnDeviceAdded(1);
    ptr->MarkSceneReady();
    return true;
  });

  a11y_view->add_view_properties_changed_callback(
      [injector = injector_weak_ptr](fuchsia::ui::gfx::ViewProperties properties) {
        auto ptr = injector.lock();
        if (!ptr) {
          return false;
        }

        // The viewport of the injector needs to match the a11y view size.
        // TODO(fxbug.dev/76667): Do proper viewport setup when possible.
        input::Injector::Viewport viewport;
        viewport.height = properties.bounding_box.max.y;
        viewport.width = properties.bounding_box.max.x;
        ptr->SetViewport(viewport);
        return true;
      });

  return injector;
}

}  // namespace a11y
