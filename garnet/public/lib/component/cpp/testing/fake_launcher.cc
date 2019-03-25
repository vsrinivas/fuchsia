// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/fake_launcher.h"

namespace component {
namespace testing {

using fuchsia::sys::Launcher;

FakeLauncher::FakeLauncher() {}

FakeLauncher::~FakeLauncher() = default;

void FakeLauncher::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  auto it = connectors_.find(launch_info.url);
  if (it != connectors_.end()) {
    it->second(std::move(launch_info), std::move(controller));
  }
}

void FakeLauncher::RegisterComponent(std::string url,
                                     ComponentConnector connector) {
  connectors_[url] = std::move(connector);
}

void FakeLauncher::Bind(fidl::InterfaceRequest<Launcher> request) {
  binding_set_.AddBinding(this, std::move(request));
}

fidl::InterfaceRequestHandler<Launcher> FakeLauncher::GetHandler() {
  return [this](fidl::InterfaceRequest<Launcher> request) {
    binding_set_.AddBinding(this, std::move(request));
  };
}

}  // namespace testing
}  // namespace component
