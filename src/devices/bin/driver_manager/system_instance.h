// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/vmo.h>

#include <fbl/span.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

#include "coordinator.h"
#include "fdio.h"
#include "fuchsia/boot/llcpp/fidl.h"

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

zx_status_t wait_for_file(const char* path, zx::time deadline);

// Host's a vfs which forwards a subset of requests to a channel.
class DirectoryFilter {
 public:
  DirectoryFilter(async_dispatcher_t* dispatcher)
      : root_dir_(fbl::MakeRefCounted<fs::PseudoDir>()), vfs_(dispatcher) {}

  // |forwarding_dir| must outlive DirectoryFilter object.
  zx_status_t Initialize(const zx::channel& forwarding_dir, fbl::Span<const char*> allow_filter);

  zx_status_t Serve(zx::channel request) {
    return vfs_.ServeDirectory(root_dir_, std::move(request));
  }

 private:
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fs::SynchronousVfs vfs_;
};

class SystemInstance : public FsProvider {
 public:
  SystemInstance();
  ~SystemInstance() { loop_.Shutdown(); }

  // Implementation required to implement FsProvider
  zx::channel CloneFs(const char* path) override;

  // The heart of the public API, in the order that things get called during
  // startup.
  zx_status_t CreateDriverHostJob(const zx::job& root_job, zx::job* driver_host_job_out);
  zx_status_t CreateSvcJob(const zx::job& root_job);

  zx_status_t StartSvchost(const zx::job& root_job, const zx::channel& root_dir,
                           bool require_system, Coordinator* coordinator);
  zx_status_t ReuseExistingSvchost();

  void devmgr_vfs_init();

  void start_services(Coordinator& coordinator);
  int ServiceStarter(Coordinator* coordinator);
  int WaitForSystemAvailable(Coordinator* coordinator);

  // TODO(fxbug.dev/34633): DEPRECATED. Do not add new dependencies on the fshost loader service!
  zx_status_t clone_fshost_ldsvc(zx::channel* loader);

 protected:
  DevmgrLauncher& launcher() { return launcher_; }
  zx::job& svc_job() { return svc_job_; }

 private:
  zx_status_t InitializeDriverHostSvcDir();

  // The outgoing (exposed) connection to the svchost.
  zx::channel svchost_outgoing_;

  // The job in which we run "svc" realm services, like svchost, netsvc, etc.
  zx::job svc_job_;

  DevmgrLauncher launcher_;

  // Hosts vfs which filters driver host svc requests to /svc provided by svchost.
  // Lazily initialized.
  std::optional<DirectoryFilter> driver_host_svc_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
