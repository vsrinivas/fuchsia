// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FS_MANAGER_H_
#define SRC_STORAGE_FSHOST_FS_MANAGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <iterator>

#include <fs/vfs.h>

#include "src/lib/loader_service/loader_service.h"

// Used for fshost signals.
#include "delayed-outdir.h"
#include "fdio.h"
#include "fshost-boot-args.h"
#include "inspect-manager.h"
#include "metrics.h"
#include "registry.h"

namespace devmgr {

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
 public:
  static zx_status_t Create(std::shared_ptr<loader::LoaderServiceBase> loader,
                            zx::channel dir_request, zx::channel lifecycle_request,
                            FsHostMetrics metrics, std::unique_ptr<FsManager>* out);

  ~FsManager();

  // TODO(fxb/39588): delete this
  // Starts servicing the delayed portion of the outgoing directory, called once
  // "/system" has been mounted.
  void FuchsiaStart() { delayed_outdir_.Start(); }

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

  // Signals FSHOST_SIGNAL_EXIT on |event_|, causing filesystems to be shutdown
  // and unmounted. Calls |callback| when this is complete.
  void Shutdown(fit::function<void(zx_status_t)> callback);

  // Returns a pointer to the |FsHostMetrics| instance.
  FsHostMetrics* mutable_metrics() { return &metrics_; }

  // Flushes FsHostMetrics to cobalt.
  void FlushMetrics();

  std::shared_ptr<devmgr::FshostBootArgs> boot_args() { return boot_args_; }

  zx::event* event() { return &event_; }

  // Creates a RemoteDir sub-directory in the fshost diagnostics directory.
  // This allows a filesystem to expose its Inspect API to Archivist alongside fshost.
  // |diagnostics_dir_name| is the name of the diagnostics subdirectory created for
  // this filesystem.
  zx_status_t AddFsDiagnosticsDirectory(const char* diagnostics_dir_name,
                                        zx::channel fs_diagnostics_dir_client);

 private:
  FsManager(FsHostMetrics metrics);
  zx_status_t SetupOutgoingDirectory(zx::channel dir_request,
                                     std::shared_ptr<loader::LoaderServiceBase> loader);
  zx_status_t SetupLifecycleServer(zx::channel lifecycle_request);
  zx_status_t Initialize();

  // Event on which "FSHOST_SIGNAL_XXX" signals are set.
  // Communicates state changes internal to FsManager.
  zx::event event_;

  static constexpr const char* kMountPoints[] = {"/bin","/data", "/volume", "/system", "/install",
                                                 "/blob", "/pkgfs", "/factory", "/durable"};
  fbl::RefPtr<fs::Vnode> mount_nodes[std::size(kMountPoints)];

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

  // Serves inspect data.
  InspectManager inspect_;

  // Used to lookup configuration options stored in fuchsia.boot.Arguments
  std::shared_ptr<devmgr::FshostBootArgs> boot_args_;

  // TODO(fxb/39588): delete this
  // A RemoteDir in the outgoing directory that ignores requests until Start is
  // called on it.
  DelayedOutdir delayed_outdir_;

  // The diagnostics directory for the fshost inspect tree.
  // Each filesystem gets a subdirectory to host their own inspect tree.
  // Archivist will parse all the inspect trees found in this directory tree.
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;

  std::unique_ptr<async::Wait> shutdown_waiter_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_FS_MANAGER_H_
