// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_APP_H_
#define GARNET_BIN_SYSMGR_APP_H_

#include <map>
#include <memory>
#include <vector>

#include <fs/managed-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/sysmgr/delegating_loader.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_namespace.h"
#include "lib/svc/cpp/services.h"

namespace sysmgr {

// The sysmgr creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
class App {
 public:
  explicit App(Config config);
  ~App();

 private:
  zx::channel OpenAsDirectory();
  void ConnectToService(const std::string& service_name, zx::channel channel);

  void RegisterSingleton(std::string service_name,
                         fuchsia::sys::LaunchInfoPtr launch_info);
  void RegisterDefaultServiceConnector();
  void RegisterAppLoaders(Config::ServiceMap app_loaders);
  void LaunchApplication(fuchsia::sys::LaunchInfo launch_info);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  // Keep track of all services, indexed by url.
  std::map<std::string, fuchsia::sys::Services> services_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fs::ManagedVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_root_;

  std::unique_ptr<DelegatingLoader> app_loader_;
  fidl::BindingSet<fuchsia::sys::Loader> app_loader_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_APP_H_
