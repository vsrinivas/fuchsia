// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_manager_app.h"

#include "garnet/bin/ui/view_manager/view_manager_impl.h"
#include "lib/fxl/logging.h"

namespace view_manager {

ViewManagerApp::ViewManagerApp()
    : startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  FXL_DCHECK(startup_context_);

  registry_.reset(new ViewRegistry(startup_context_.get()));

  startup_context_->outgoing()
      .AddPublicService<::fuchsia::ui::viewsv1::ViewManager>(
          [this](fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewManager>
                     request) {
            view_manager_bindings_.AddBinding(
                std::make_unique<ViewManagerImpl>(registry_.get()),
                std::move(request));
          });
}

ViewManagerApp::~ViewManagerApp() {}

}  // namespace view_manager
