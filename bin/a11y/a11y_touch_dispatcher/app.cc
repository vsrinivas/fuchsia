// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_touch_dispatcher/app.h"

namespace a11y_touch_dispatcher {

App::App()
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      touch_dispatcher_(std::make_unique<A11yTouchDispatcherImpl>()) {
  startup_context_->outgoing()
      .AddPublicService<fuchsia::accessibility::InputReceiver>(
          [this](fidl::InterfaceRequest<fuchsia::accessibility::InputReceiver>
                     request) {
            touch_dispatcher_->BindInputReceiver(std::move(request));
          });
  startup_context_->outgoing()
      .AddPublicService<fuchsia::accessibility::TouchDispatcher>(
          [this](fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher>
                     request) {
            touch_dispatcher_->BindTouchDispatcher(std::move(request));
          });
}

}  // namespace a11y_touch_dispatcher