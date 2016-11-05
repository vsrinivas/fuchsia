// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_manager_app.h"

#include "apps/mozart/src/input_manager/input_associate.h"
#include "lib/ftl/logging.h"

namespace input_manager {

InputManagerApp::InputManagerApp()
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);

  application_context_->outgoing_services()->AddService<mozart::ViewAssociate>(
      [this](fidl::InterfaceRequest<mozart::ViewAssociate> request) {
        associate_bindings_.AddBinding(std::make_unique<InputAssociate>(),
                                       std::move(request));
      });
}

InputManagerApp::~InputManagerApp() {}

}  // namespace input_manager
