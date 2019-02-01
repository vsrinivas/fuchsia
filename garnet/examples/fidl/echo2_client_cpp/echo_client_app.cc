// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_client_app.h"

#include "lib/component/cpp/startup_context.h"

namespace echo2 {

EchoClientApp::EchoClientApp()
    : EchoClientApp(component::StartupContext::CreateFromStartupInfo()) {}

EchoClientApp::EchoClientApp(std::unique_ptr<component::StartupContext> context)
    : context_(std::move(context)) {}

void EchoClientApp::Start(std::string server_url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = server_url;
  launch_info.directory_request = echo_provider_.NewRequest();
  context_->launcher()->CreateComponent(std::move(launch_info),
                                        controller_.NewRequest());

  echo_provider_.ConnectToService(echo_.NewRequest().TakeChannel(),
                                  fidl::examples::echo::Echo::Name_);
}

}  // namespace echo2
