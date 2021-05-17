// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/injector_config_setup.h"

#include <lib/syslog/cpp/macros.h>

namespace root_presenter {

InjectorConfigSetup::InjectorConfigSetup(sys::ComponentContext* component_context,
                                         fuchsia::ui::views::ViewRef context,
                                         fuchsia::ui::views::ViewRef target)
    : binding_(this),
      context_(std::move(context)),
      target_(std::move(target)),
      watch_viewport_callback_(nullptr) {
  FX_DCHECK(component_context);
  component_context->outgoing()
      ->AddPublicService<fuchsia::ui::pointerinjector::configuration::Setup>(
          [this](
              fidl::InterfaceRequest<fuchsia::ui::pointerinjector::configuration::Setup> request) {
            if (binding_.is_bound()) {
              FX_LOGS(WARNING) << "Pointer injector setup is already bound.";
            } else {
              binding_.Bind(std::move(request));
              binding_.set_error_handler(
                  [this](zx_status_t status) {
                    FX_LOGS(ERROR)
                        << "Disconnected from fuchsia::ui::pointerinjector::configuration::Setup. "
                        << "Status: " << status;
                    binding_.Unbind();
                    watch_viewport_callback_ = nullptr;
                  });
            }
          });
}

void InjectorConfigSetup::GetViewRefs(GetViewRefsCallback callback) {
  callback(fidl::Clone(context_), fidl::Clone(target_));
}

void InjectorConfigSetup::WatchViewport(WatchViewportCallback callback) {
  // Fail if this is called before the previous call responded.
  if (watch_viewport_callback_) {
    FX_LOGS(ERROR) << "Client called WatchViewport() while a previous call was still pending.";
    binding_.Close(ZX_ERR_BAD_STATE);
    watch_viewport_callback_ = nullptr;
    return;
  }

  // Send the viewport immediately if there was an update since the last call to
  // WatchViewport().
  if (viewport_.has_value()) {
    callback(std::move(viewport_.value()));
  } else {
    watch_viewport_callback_ = std::move(callback);
  }
}

void InjectorConfigSetup::UpdateViewport(fuchsia::ui::pointerinjector::Viewport viewport) {
  // Send the updated viewport if there is an outstanding WatchViewportCallback.
  if (watch_viewport_callback_) {
    watch_viewport_callback_(std::move(viewport));
    watch_viewport_callback_ = nullptr;
  } else {
    viewport_ = std::move(viewport);
  }
}

}  // namespace root_presenter
