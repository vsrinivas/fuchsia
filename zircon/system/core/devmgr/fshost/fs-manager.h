// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FS_MANAGER_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FS_MANAGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/boot-args/boot-args.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fs/vfs.h>
#include <loader-service/loader-service.h>

// Used for fshost signals.
#include "../shared/fdio.h"
#include "fshost-boot-args.h"
#include "metrics.h"
#include "registry.h"

namespace devmgr {

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
 public:
  static zx_status_t Create(zx::event fshost_event, loader_service_t* loader_svc,
                            zx::channel dir_request, FsHostMetrics metrics,
                            std::unique_ptr<FsManager>* out);

  // Set of options for logging FsHost metrics with cobalt service.
  static cobalt_client::CollectorOptions CollectorOptions();

  ~FsManager();

  // Signals that "/system" has been mounted.
  void FuchsiaStart() const { event_.signal(0, FSHOST_SIGNAL_READY); }

  // Pins a handle to a remote filesystem on one of the paths specified
  // by |kMountPoints|.
  zx_status_t InstallFs(const char* path, zx::channel h);

  // Serves connection to the root directory ("/") on |server|.
  zx_status_t ServeRoot(zx::channel server);

  // Serves connection to the fshost directory (exporting the "fuchsia.fshost" services) on
  // |server|.
  zx_status_t ServeFshostRoot(zx::channel server) { return registry_.ServeRoot(std::move(server)); }

  // Triggers unmount when the FSHOST_SIGNAL_EXIT signal is raised on |event_|.
  //
  // Sets FSHOST_SIGNAL_EXIT_DONE when unmounting is complete.
  void WatchExit();

  // Returns a pointer to the |FsHostMetrics| instance.
  FsHostMetrics* mutable_metrics() { return &metrics_; }

  // Flushes FsHostMetrics to cobalt.
  void FlushMetrics();

  devmgr::FshostBootArgs* boot_args() { return &boot_args_; }

 private:
  FsManager(zx::event fshost_event, FsHostMetrics metrics);
  zx_status_t SetupOutgoingDirectory(zx::channel dir_request, loader_service_t* loader_svc);
  zx_status_t Initialize();

  // Event on which "FSHOST_SIGNAL_XXX" signals are set.
  // Communicates state changes to/from devmgr.
  zx::event event_;

  static constexpr const char* kMountPoints[] = {"/bin",     "/data", "/volume", "/system",
                                                 "/install", "/blob", "/pkgfs"};
  fbl::RefPtr<fs::Vnode> mount_nodes[fbl::count_of(kMountPoints)];

  // The Root VFS manages the following filesystems:
  // - The global root filesystem (including the mount points)
  // - "/tmp"
  std::unique_ptr<memfs::Vfs> root_vfs_;

  std::unique_ptr<async::Loop> global_loop_;
  fs::ManagedVfs outgoing_vfs_;
  async::Wait global_shutdown_;

  // The base, root directory which serves the rest of the fshost.
  fbl::RefPtr<memfs::VnodeDir> global_root_;

  // Controls the external fshost vnode, as well as registration of filesystems
  // dynamically within the fshost.
  fshost::Registry registry_;

  // Keeps a collection of metrics being track at the FsHost level.
  FsHostMetrics metrics_;

  // Used to lookup configuration options stored in fuchsia.boot.Arguments
  devmgr::FshostBootArgs boot_args_;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FS_MANAGER_H_
