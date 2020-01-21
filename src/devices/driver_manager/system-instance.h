// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
#define SRC_DEVICES_DRIVER_MANAGER_SYSTEM_INSTANCE_H_

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/boot-args/boot-args.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/vmo.h>

#include "coordinator.h"
#include "fdio.h"

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

zx_status_t wait_for_file(const char* path, zx::time deadline);

class SystemInstance : public devmgr::FsProvider {
 public:
  struct ServiceStarterArgs {
    SystemInstance* instance;
    devmgr::Coordinator* coordinator;
  };

  SystemInstance();

  // Implementation required to implement devmgr::FsProvider
  zx::channel CloneFs(const char* path) override;

  // The heart of the public API, in the order that things get called during
  // startup.
  zx_status_t CreateSvcJob(const zx::job& root_job);
  zx_status_t PrepareChannels();

  zx_status_t StartSvchost(const zx::job& root_job, bool require_system,
                           devmgr::Coordinator* coordinator);
  zx_status_t ReuseExistingSvchost();

  void devmgr_vfs_init();

  // Thread entry point
  static int pwrbtn_monitor_starter(void* arg);
  int PwrbtnMonitorStarter(devmgr::Coordinator* coordinator);

  void start_console_shell(const devmgr::BootArgs& boot_args);
  int ConsoleStarter(const devmgr::BootArgs* arg);

  // Thread entry point
  static int service_starter(void* arg);
  int ServiceStarter(devmgr::Coordinator* coordinator);
  int WaitForSystemAvailable(devmgr::Coordinator* coordinator);

  // TODO(ZX-4860): DEPRECATED. Do not add new dependencies on the fshost loader service!
  zx_status_t clone_fshost_ldsvc(zx::channel* loader);

 protected:
  // Protected constructor for SystemInstance that allows injecting a different
  // namespace, primarily for use in unit tests.
  explicit SystemInstance(fdio_ns_t* default_ns);

 private:
  // Private helper functions.
  void do_autorun(const char* name, const char* cmd, const zx::resource& root_resource);

  // The handle used to transmit messages to miscsvc.
  zx::channel miscsvc_client_;

  // The handle used by miscsvc to serve incoming requests.
  zx::channel miscsvc_server_;

  // The handle used to transmit messages to device_name_provider.
  zx::channel device_name_provider_client_;

  // The handle used by device_name_provider to serve incoming requests.
  zx::channel device_name_provider_server_;

  // The outgoing (exposed) connection to the svchost.
  zx::channel svchost_outgoing_;

  // The job in which we run "svc" realm services, like svchost, fshost,
  // miscsvc, netsvc, the consoles, autorun, and others.
  zx::job svc_job_;

  // Used to bind the svchost to the virtual-console binary to provide fidl
  // services.
  zx::channel virtcon_fidl_;

  // The namespace into which SystemInstance::CloneFs will send open requests
  // for directories hosted by fshost. Defaults to the results of
  // fdio_ns_get_installed during construction, but can be overridden for test
  // cases.
  fdio_ns_t* default_ns_;

  devmgr::DevmgrLauncher launcher_;
};

#endif  // SRC_DEVICES_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
