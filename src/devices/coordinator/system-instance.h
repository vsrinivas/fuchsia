// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_COORDINATOR_SYSTEM_INSTANCE_H_
#define SRC_DEVICES_COORDINATOR_SYSTEM_INSTANCE_H_

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/zx/vmo.h>

#include "boot-args.h"
#include "coordinator.h"
#include "fdio.h"

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

zx_status_t wait_for_file(const char* path, zx::time deadline);
zx_status_t get_ramdisk(zx::vmo* ramdisk_vmo);

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
  zx_status_t CreateFuchsiaJob(const zx::job& root_job);
  zx_status_t PrepareChannels();

  zx_status_t StartSvchost(const zx::job& root_job, bool require_system,
                           devmgr::Coordinator* coordinator, zx::channel fshost_client);
  zx_status_t ReuseExistingSvchost();

  void devmgr_vfs_init(devmgr::Coordinator* coordinator, const devmgr::DevmgrArgs& devmgr_args,
                       zx::channel fshost_server);
  // Thread entry point
  static int pwrbtn_monitor_starter(void* arg);
  int PwrbtnMonitorStarter(devmgr::Coordinator* coordinator);

  void start_console_shell(const devmgr::BootArgs& boot_args);
  int ConsoleStarter(const devmgr::BootArgs* arg);

  // Thread entry point
  static int service_starter(void* arg);
  int ServiceStarter(devmgr::Coordinator* coordinator);
  int FuchsiaStarter(devmgr::Coordinator* coordinator);

  // TODO(ZX-4860): DEPRECATED. Do not add new dependencies on the fshost loader service!
  zx_status_t clone_fshost_ldsvc(zx::channel* loader);

 protected:
  // Protected constructor for SystemInstance that allows injecting a filesystem root, primarily for
  // use in unit tests.
  explicit SystemInstance(zx::channel fs_root);

 private:
  // Private helper functions.
  void do_autorun(const char* name, const char* cmd, const zx::resource& root_resource);
  void fshost_start(devmgr::Coordinator* coordinator, const devmgr::DevmgrArgs& devmgr_args,
                    zx::channel fshost_server);

  // The handle used to transmit messages to appmgr.
  zx::channel appmgr_client_;

  // The handle used by appmgr to serve incoming requests.
  // If appmgr cannot be launched within a timeout, this handle is closed.
  zx::channel appmgr_server_;

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

  // Handle to the loader service hosted in fshost, which allows loading from /boot and /system
  // rather than specific packages.
  // This isn't actually "optional", it's just initialized later.
  // TODO(ZX-4860): Delete this once all dependencies have been removed.
  fit::optional<llcpp::fuchsia::ldsvc::Loader::SyncClient> fshost_ldsvc_;

  // The root of the filesystem host.
  zx::channel fs_root_;

  // The job in which we run "svc" realm services, like svchost, fshost,
  // miscsvc, netsvc, the consoles, autorun, and others.
  zx::job svc_job_;

  // The job in which we run appmgr.
  zx::job fuchsia_job_;

  // Used to bind the svchost to the virtual-console binary to provide fidl
  // services.
  zx::channel virtcon_fidl_;

  devmgr::DevmgrLauncher launcher_;
};

#endif  // SRC_DEVICES_COORDINATOR_SYSTEM_INSTANCE_H_
