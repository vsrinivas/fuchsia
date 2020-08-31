// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_SYSMGR_APP_H_
#define SRC_SYS_SYSMGR_APP_H_

#include <fuchsia/pkg/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/time.h>

#include <deque>
#include <map>
#include <memory>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/sys/sysmgr/config.h"
#include "src/sys/sysmgr/package_updating_loader.h"

namespace sysmgr {

// The sysmgr creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
class App {
 public:
  explicit App(Config config, std::shared_ptr<sys::ServiceDirectory> incoming_services,
               async::Loop* loop);
  ~App();

  // Launch component in the sys realm.
  // If the component is marked as critical, it is tracked and restarted when it crashes.
  void LaunchComponent(const fuchsia::sys::LaunchInfo& launch_info,
                       fuchsia::sys::ComponentController::OnTerminatedCallback on_terminate,
                       fit::function<void(zx_status_t)> on_ctrl_err);

 private:
  zx::channel OpenAsDirectory();
  void ConnectToService(const std::string& service_name, zx::channel channel);

  void RegisterSingleton(std::string service_name, fuchsia::sys::LaunchInfoPtr launch_info,
                         bool is_optional_service);
  void RegisterLoader();
  void RegisterDefaultServiceConnector();

  void RestartCriticalComponent(const std::string& component_url);

  void StartDiagnostics(fuchsia::sys::LaunchInfo launch_diagnostics, async::Loop* loop);

  async::Loop* loop_;
  std::shared_ptr<sys::ServiceDirectory> incoming_services_;

  // Keep track of services provided by each cmoponent URL.
  std::map<std::string, std::shared_ptr<sys::ServiceDirectory>> services_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  vfs::PseudoDir svc_root_;
  std::vector<std::string> svc_names_;

  std::unique_ptr<PackageUpdatingLoader> package_updating_loader_;
  fuchsia::sys::LoaderPtr loader_;
  fidl::BindingSet<fuchsia::sys::Loader> loader_bindings_;

  bool auto_updates_enabled_;

  // A record of critical components.
  //
  // All critical compnoents have entries in this map, regardless of if they're running.
  // The associated |ComponentController| may be `nullptr` if it is not running.
  struct CriticalComponentRuntimeInfo {
    fuchsia::sys::LaunchInfo latest_launch_info;
    std::deque<zx::time> crash_history;
  };
  std::unordered_map<std::string, CriticalComponentRuntimeInfo> critical_components_;
  // Contains the ComponentControllers of all component URLs launched by sysmgr.
  // They are removed from this map when the associated component dies.
  std::unordered_map<std::string, fuchsia::sys::ComponentControllerPtr> controllers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace sysmgr

#endif  // SRC_SYS_SYSMGR_APP_H_
