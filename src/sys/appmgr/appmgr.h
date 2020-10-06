// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_APPMGR_H_
#define SRC_SYS_APPMGR_APPMGR_H_

#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/service_directory.h>

#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

#include "lib/async/cpp/executor.h"
#include "src/lib/fxl/macros.h"
#include "src/sys/appmgr/cpu_watcher.h"
#include "src/sys/appmgr/lifecycle.h"
#include "src/sys/appmgr/moniker.h"
#include "src/sys/appmgr/realm.h"
#include "src/sys/appmgr/startup_service.h"
#include "src/sys/appmgr/storage_watchdog.h"
#include "src/sys/appmgr/util.h"

namespace component {

struct AppmgrArgs {
  // outgoing service directory
  zx_handle_t pa_directory_request;
  zx_handle_t lifecycle_request;
  std::unordered_set<Moniker> lifecycle_allowlist;
  fuchsia::sys::ServiceListPtr root_realm_services;
  const std::shared_ptr<sys::ServiceDirectory> environment_services;
  std::string sysmgr_url;
  fidl::VectorPtr<std::string> sysmgr_args;
  std::optional<fuchsia::sys::LoaderPtr> loader;
  bool run_virtual_console;
  zx::channel trace_server_channel;
  // This function is called after appmgr completes its stop logic
  fit::function<void(zx_status_t)> stop_callback;
};

struct LifecycleComponent {
  std::shared_ptr<ComponentControllerImpl> controller;
  Moniker moniker;

  LifecycleComponent(std::shared_ptr<ComponentControllerImpl> controller, Moniker moniker)
      : controller(controller), moniker(moniker) {}
};

class Appmgr {
 public:
  Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args);
  ~Appmgr();

  // Called as part of the process lifecycle allowing appmgr to cleanly shutdown child components
  // that support the process lifecycle protocol.
  // Calls |callback| when this is complete.
  void Shutdown(fit::function<void(zx_status_t)> callback);

  Realm* RootRealm() { return root_realm_.get(); }

  bool is_sysmgr_running() { return sysmgr_running_; }

 private:
  // Initialize recording of appmgr's own CPU usage in the CpuWatcher.
  void RecordSelfCpuStats();

  // Take a CPU measurement.
  void MeasureCpu(async_dispatcher_t* dispatcher);

  // Search for components that are in the lifecycle_allowlist_
  void FindLifecycleComponentsInRealm(Realm* realm,
                                      std::vector<LifecycleComponent>* lifecycle_components);

  inspect::Inspector inspector_;
  std::unique_ptr<CpuWatcher> cpu_watcher_;
  std::unique_ptr<Realm> root_realm_;
  fs::SynchronousVfs publish_vfs_;
  fbl::RefPtr<fs::PseudoDir> publish_dir_;

  fuchsia::sys::ComponentControllerPtr sysmgr_;
  std::string sysmgr_url_;
  fidl::VectorPtr<std::string> sysmgr_args_;
  bool sysmgr_running_ = false;
  StorageWatchdog storage_watchdog_;

  LifecycleServer lifecycle_server_;
  async::Executor lifecycle_executor_;
  std::unordered_set<Moniker> lifecycle_allowlist_;
  StartupServiceImpl startup_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Appmgr);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_APPMGR_H_
