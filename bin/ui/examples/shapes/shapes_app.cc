// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shapes/shapes_app.h"

#include "apps/mozart/examples/shapes/shapes_view.h"
#include "mojo/public/cpp/application/connect.h"

namespace examples {

ShapesApp::ShapesApp() {}

ShapesApp::~ShapesApp() {}

void ShapesApp::CreateView(
    const std::string& connection_url,
    mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
    mojo::InterfaceRequest<mojo::ServiceProvider> services) {
  new ShapesView(mojo::CreateApplicationConnector(shell()),
                 view_owner_request.Pass());
}

}  // namespace examples
