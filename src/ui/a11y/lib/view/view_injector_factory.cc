// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_injector_factory.h"

namespace a11y {

std::unique_ptr<input::Injector> ViewInjectorFactory::BuildAndConfigureInjector(
    AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
    fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
  auto view_properties = a11y_view->get_a11y_view_properties();
  if (!view_properties) {
    return nullptr;
  }

  auto injector =
      std::make_unique<input::Injector>(component_context, std::move(context), std::move(target));
  // TODO(fxbug.dev/77608): Verify viewport configuration to match a11y view.
  input::Injector::Viewport viewport;
  viewport.height = view_properties->bounding_box.max.y;
  viewport.width = view_properties->bounding_box.max.x;
  injector->SetViewport(viewport);

  // Note: a11y uses a device id == 1 here since only one touch screen device is supported at the
  // moment.
  injector->OnDeviceAdded(1);
  injector->MarkSceneReady();
  return injector;
}

}  // namespace a11y
