// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/view_manager/view_manager_app.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "mojo/common/tracing_impl.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "services/ui/view_manager/view_manager_impl.h"

namespace view_manager {

ViewManagerApp::ViewManagerApp() {}

ViewManagerApp::~ViewManagerApp() {}

void ViewManagerApp::OnInitialize() {
  auto command_line = base::CommandLine::ForCurrentProcess();
  command_line->InitFromArgv(args());
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  tracing_.Initialize(shell(), &args());

  // Connect to compositor.
  mojo::gfx::composition::CompositorPtr compositor;
  mojo::ConnectToService(shell(), "mojo:compositor_service",
                         GetProxy(&compositor));
  compositor.set_connection_error_handler(base::Bind(
      &ViewManagerApp::OnCompositorConnectionError, base::Unretained(this)));

  // Create the registry.
  registry_.reset(new ViewRegistry(compositor.Pass()));
}

bool ViewManagerApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<mojo::ui::ViewManager>([this](
      const mojo::ConnectionContext& connection_context,
      mojo::InterfaceRequest<mojo::ui::ViewManager> view_manager_request) {
    DCHECK(registry_);
    view_managers_.AddBinding(new ViewManagerImpl(registry_.get()),
                              view_manager_request.Pass());
  });
  return true;
}

void ViewManagerApp::OnCompositorConnectionError() {
  LOG(ERROR) << "Exiting due to compositor connection error.";
  Shutdown();
}

void ViewManagerApp::Shutdown() {
  mojo::TerminateApplication(MOJO_RESULT_OK);
}

}  // namespace view_manager
