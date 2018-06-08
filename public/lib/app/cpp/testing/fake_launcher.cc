// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_launcher.h"

namespace fuchsia {
namespace sys {
namespace testing {

void FakeLauncher::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  auto it = connectors_.find(launch_info.url);
  if (it != connectors_.end()) {
    it->second(std::move(launch_info), std::move(controller));
  }
}

void FakeLauncher::RegisterComponent(std::string url,
                                     ComponentConnectorFn connector) {
  connectors_[url] = connector;
}

void FakeLauncher::Bind(fidl::InterfaceRequest<Launcher> request) {
  binding_.Bind(std::move(request));
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
