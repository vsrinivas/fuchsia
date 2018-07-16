// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_SCOPE_H_
#define LIB_TEST_RUNNER_CPP_SCOPE_H_

#include <memory>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/component/cpp/service_provider_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace test_runner {

// Provides fate separation of sets of applications run by one application. The
// environment services are delegated to the parent environment.
class Scope {
 public:
  Scope(const fuchsia::sys::EnvironmentPtr& parent_env,
        const std::string& label);

  template <typename Interface>
  void AddService(fidl::InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    service_provider_bridge_.AddService(std::move(handler), service_name);
  }

  fuchsia::sys::Launcher* GetLauncher();

  fuchsia::sys::EnvironmentPtr& environment() { return env_; }

 private:
  component::ServiceProviderBridge service_provider_bridge_;
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::LauncherPtr env_launcher_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
};

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_SCOPE_H_
