// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_
#define PERIDOT_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_

#include <map>
#include <string>

#include <component/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace modular {
namespace testing {

class FakeApplicationLauncher : public component::ApplicationLauncher {
 public:
  using ApplicationConnectorFn = std::function<void(
      component::LaunchInfo,
      fidl::InterfaceRequest<component::ComponentController>)>;

  // Registers an application located at "url" with a connector. When someone
  // tries to CreateApplication() with this |url|, the supplied |connector| is
  // called with the the LaunchInfo and associated
  // ComponentController request. The connector may implement the
  // |LaunchInfo.services| and |ComponentController| interfaces to
  // communicate with its connector and listen for application closing signals
  void RegisterApplication(std::string url, ApplicationConnectorFn connector);

 private:
  // Forwards this |CreateApplication| request to a registered connector, if an
  // associated one exists. If one is not registered for |launch_info.url|, then
  // this call is dropped.
  // |ApplicationLauncher|
  void CreateApplication(
      component::LaunchInfo launch_info,
      fidl::InterfaceRequest<component::ComponentController> controller)
      override;

  std::map<std::string, ApplicationConnectorFn> connectors_;
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_
