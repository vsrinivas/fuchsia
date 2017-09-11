// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_
#define APPS_MODULAR_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_

#include <string>
#include <unordered_map>

#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace testing {

class FakeApplicationLauncher : public app::ApplicationLauncher {
 public:
  using ApplicationConnectorFn =
      std::function<void(app::ApplicationLaunchInfoPtr,
                         fidl::InterfaceRequest<app::ApplicationController>)>;

  // Registers an application located at "url" with a connector. When someone
  // tries to CreateApplication() with this |url|, the supplied |connector| is
  // called with the the ApplicationLaunchInfoPtr and associated
  // ApplicationController request. The connector may implement the
  // |ApplicationLaunchInfo.services| and |ApplicationController| interfaces to
  // communicate with its connector and listen for application closing signals
  void RegisterApplication(std::string url, ApplicationConnectorFn connector);

 private:
  // Forwards this |CreateApplication| request to a registered connector, if an
  // associated one exists. If one is not registered for |launch_info.url|, then
  // this call is dropped.
  // |ApplicationLauncher|
  void CreateApplication(
      app::ApplicationLaunchInfoPtr launch_info,
      fidl::InterfaceRequest<app::ApplicationController> controller) override;

  std::unordered_map<std::string, ApplicationConnectorFn> connectors_;
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_FAKE_APPLICATION_LAUNCHER_H_
