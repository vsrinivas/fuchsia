// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_

#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/vmo.h>

#include <fbl/span.h>

#include "coordinator.h"
#include "fdio.h"
#include "fuchsia/boot/llcpp/fidl.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

zx_status_t wait_for_file(const char* path, zx::time deadline);

// Host's a vfs which forwards a subset of requests to a channel.
class DirectoryFilter {
 public:
  DirectoryFilter(async_dispatcher_t* dispatcher)
      : root_dir_(fbl::MakeRefCounted<fs::PseudoDir>()), vfs_(dispatcher) {}

  zx_status_t Initialize(zx::channel forwarding_dir, fbl::Span<const char*> allow_filter);

  zx_status_t Serve(zx::channel request) {
    return vfs_.ServeDirectory(root_dir_, std::move(request));
  }

 private:
  zx::channel forwarding_dir_;
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fs::SynchronousVfs vfs_;
};

class SystemInstance : public FsProvider {
 public:
  SystemInstance();
  ~SystemInstance() { loop_.Shutdown(); }

  // Implementation required to implement FsProvider
  zx::channel CloneFs(const char* path) override;

  zx_status_t CreateDriverHostJob(const zx::job& root_job, zx::job* driver_host_job_out);
  void InstallDevFsIntoNamespace();

  void ServiceStarter(Coordinator* coordinator);
  int WaitForSystemAvailable(Coordinator* coordinator);

  // TODO(fxbug.dev/34633): DEPRECATED. Do not add new dependencies on the fshost loader service!
  zx_status_t clone_fshost_ldsvc(zx::channel* loader);

 protected:
  DevmgrLauncher& launcher() { return launcher_; }

 private:
  zx_status_t InitializeDriverHostSvcDir();

  DevmgrLauncher launcher_;

  // Hosts vfs which filters driver host svc requests to /svc provided by svchost.
  // Lazily initialized.
  std::optional<DirectoryFilter> driver_host_svc_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_INSTANCE_H_
