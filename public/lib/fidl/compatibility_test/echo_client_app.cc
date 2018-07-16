// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/fidl/compatibility_test/echo_client_app.h"
#include <fidl/test/compatibility/cpp/fidl.h>

namespace fidl {
namespace test {
namespace compatibility {

EchoClientApp::EchoClientApp()
    : context_(component::StartupContext::CreateFromStartupInfo()) {}

EchoPtr& EchoClientApp::echo() { return echo_; }

void EchoClientApp::Start(std::string server_url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = server_url;
  launch_info.directory_request = echo_provider_.NewRequest();
  context_->launcher()->CreateComponent(std::move(launch_info),
                                        controller_.NewRequest());

  echo_provider_.ConnectToService(echo_.NewRequest().TakeChannel(),
                                  Echo::Name_);
}
}  // namespace compatibility
}  // namespace test
}  // namespace fidl
