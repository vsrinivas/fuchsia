// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TESTING_FAKE_LAUNCHER_H_
#define LIB_COMPONENT_CPP_TESTING_FAKE_LAUNCHER_H_

#include <fs/service.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"

namespace component {
namespace testing {

// A fake |StartupContext| for testing.
// Does not allow publishing or accessing services outside of the test
// environment.
class FakeLauncher : public fuchsia::sys::Launcher {
 public:
  FakeLauncher();
  ~FakeLauncher() override;

  FakeLauncher(const FakeLauncher&) = delete;
  FakeLauncher& operator=(const FakeLauncher&) = delete;

  using ComponentConnector = fit::function<void(
      fuchsia::sys::LaunchInfo,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController>)>;

  // Registers a component located at "url" with a connector. When someone
  // tries to CreateComponent() with this |url|, the supplied |connector| is
  // called with the the LaunchInfo and associated ComponentController request.
  // The connector may implement the |LaunchInfo.services| and
  // |ComponentController| interfaces to communicate with its connector and
  // listen for component signals.
  void RegisterComponent(std::string url, ComponentConnector connector);

  // Forwards this |CreateComponent| request to a registered connector, if an
  // associated one exists. If one is not registered for |launch_info.url|, then
  // this call is dropped.
  // TODO(CP-57): this needs to be idempotent.
  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                           controller) override;

  fidl::InterfaceRequestHandler<fuchsia::sys::Launcher> GetHandler();

 private:
  std::map<std::string, ComponentConnector> connectors_;
  fidl::BindingSet<Launcher> binding_set_;
  friend class StartupContextForTest;

  void Bind(fidl::InterfaceRequest<Launcher> request);
};

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_FAKE_LAUNCHER_H_
