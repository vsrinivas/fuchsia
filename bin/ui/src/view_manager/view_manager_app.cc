// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_manager_app.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/view_manager/view_manager_impl.h"
#include "lib/ftl/command_line.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace view_manager {

ViewManagerApp::ViewManagerApp() {}

ViewManagerApp::~ViewManagerApp() {}

void ViewManagerApp::OnInitialize() {
  auto command_line = ftl::CommandLineFromIteratorsWithArgv0(
      url(), args().begin(), args().end());

  // Connect to compositor.
  mozart::CompositorPtr compositor;
  mojo::ConnectToService(shell(), "mojo:compositor_service",
                         GetProxy(&compositor));
  compositor.set_connection_error_handler(
      [this] { OnCompositorConnectionError(); });

  // Create the registry.
  registry_.reset(new ViewRegistry(compositor.Pass()));
}

bool ViewManagerApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<mozart::ViewManager>(
      [this](const mojo::ConnectionContext& connection_context,
             mojo::InterfaceRequest<mozart::ViewManager> view_manager_request) {
        FTL_DCHECK(registry_);
        view_managers_.AddBinding(new ViewManagerImpl(registry_.get()),
                                  view_manager_request.Pass());
      });
  return true;
}

void ViewManagerApp::OnCompositorConnectionError() {
  FTL_LOG(ERROR) << "Exiting due to compositor connection error.";
  Shutdown();
}

void ViewManagerApp::Shutdown() {
  mojo::TerminateApplication(MOJO_RESULT_OK);
}

}  // namespace view_manager
