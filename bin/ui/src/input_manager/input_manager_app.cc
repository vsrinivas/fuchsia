// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/input_manager/input_manager_app.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "mojo/common/tracing_impl.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "services/ui/input_manager/input_associate.h"

namespace input_manager {

InputManagerApp::InputManagerApp() {}

InputManagerApp::~InputManagerApp() {}

void InputManagerApp::OnInitialize() {
  auto command_line = base::CommandLine::ForCurrentProcess();
  command_line->InitFromArgv(args());
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  tracing_.Initialize(shell(), &args());
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
