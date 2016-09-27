// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_manager_app.h"

#include "apps/mozart/src/input_manager/input_associate.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace input_manager {

InputManagerApp::InputManagerApp() {}

InputManagerApp::~InputManagerApp() {}

void InputManagerApp::OnInitialize() {
  // TODO(mikejurka): Initialize logging and tracing.
}

bool InputManagerApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<mojo::ui::ViewAssociate>([this](
      const mojo::ConnectionContext& connection_context,
      mojo::InterfaceRequest<mojo::ui::ViewAssociate> view_associate_request) {
    input_associates_.AddBinding(new InputAssociate(),
                                 view_associate_request.Pass());
  });
  return true;
}

}  // namespace input_manager
