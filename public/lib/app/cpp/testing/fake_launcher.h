// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_FAKE_LAUNCHER_H_
#define LIB_APP_CPP_TESTING_FAKE_LAUNCHER_H_

#include <fs/service.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"

namespace fuchsia {
namespace sys {
namespace testing {

// A fake |StartupContext| for testing.
// Does not allow publishing or accessing services outside of the test
// environment.
class FakeLauncher : public fuchsia::sys::Launcher {
 public:
  FakeLauncher() : binding_(this) {}

  using ComponentConnectorFn = std::function<void(
      fuchsia::sys::LaunchInfo,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController>)>;

  // Registers a component located at "url" with a connector. When someone
  // tries to CreateComponent() with this |url|, the supplied |connector| is
  // called with the the LaunchInfo and associated ComponentController request.
  // The connector may implement the |LaunchInfo.services| and
  // |ComponentController| interfaces to communicate with its connector and
  // listen for component signals.
  // TODO(CP-57): make this easier to use.
  void RegisterComponent(std::string url, ComponentConnectorFn connector);

  // Forwards this |CreateComponent| request to a registered connector, if an
  // associated one exists. If one is not registered for |launch_info.url|, then
  // this call is dropped.
  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                           controller) override;

 private:
  std::map<std::string, ComponentConnectorFn> connectors_;
  fidl::Binding<Launcher> binding_;
  friend class StartupContextForTest;

  void Bind(fidl::InterfaceRequest<Launcher> request);
};

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_TESTING_FAKE_LAUNCHER_H_