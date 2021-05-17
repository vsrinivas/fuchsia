// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_CONFIG_SETUP_H_
#define SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_CONFIG_SETUP_H_

#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

namespace root_presenter {

// Allows an input pipeline to access pointer injection configuration details.
class InjectorConfigSetup : public fuchsia::ui::pointerinjector::configuration::Setup {
 public:
  explicit InjectorConfigSetup(sys::ComponentContext* component_context,
                               fuchsia::ui::views::ViewRef context,
                               fuchsia::ui::views::ViewRef target);

  // |fuchsia.ui.pointerinjector.configuration.Setup|
  void GetViewRefs(GetViewRefsCallback callback) override;

  // |fuchsia.ui.pointerinjector.configuration.Setup|
  void WatchViewport(WatchViewportCallback callback) override;

  // If |watch_viewport_callback_| exists, calls it with the given viewport.
  // Otherwise, sets |viewport_|.
  void UpdateViewport(fuchsia::ui::pointerinjector::Viewport viewport);

 private:
  fidl::Binding<fuchsia::ui::pointerinjector::configuration::Setup> binding_;

  fuchsia::ui::views::ViewRef context_;
  fuchsia::ui::views::ViewRef target_;

  // Only store the viewport if it has been updated.
  std::optional<fuchsia::ui::pointerinjector::Viewport> viewport_;
  fit::function<void(fuchsia::ui::pointerinjector::Viewport)> watch_viewport_callback_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_CONFIG_SETUP_H_
