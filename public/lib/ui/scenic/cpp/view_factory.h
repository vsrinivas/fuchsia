// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_VIEW_FACTORY_H_
#define LIB_UI_SCENIC_CPP_VIEW_FACTORY_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <functional>
#include <vector>

#include "lib/component/cpp/startup_context.h"

#include "lib/ui/scenic/cpp/base_view.h"
#include "lib/ui/scenic/cpp/session.h"

namespace scenic {

// Parameters for creating a view.
struct ViewFactoryArgs {
  scenic::SessionPtrAndListenerRequest session_and_listener_request;
  zx::eventpair view_token;
  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services;
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services;
  component::StartupContext* startup_context;
};

// A callback to create a view in response to a call to
// |ViewProvider.Create()|.
using ViewFactory =
    std::function<std::unique_ptr<BaseView>(ViewFactoryArgs args)>;

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_VIEW_FACTORY_H_
