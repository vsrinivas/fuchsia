// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "../shared/env.h"
#include "../shared/fdio.h"

#include "registry.h"

namespace devmgr {

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
public:
    static zx_status_t Create(zx::event fshost_event, fbl::unique_ptr<FsManager>* out);

    // Signals that "/system" has been mounted.
    void FuchsiaStart() const { event_.signal(0, FSHOST_SIGNAL_READY); }

    // Pins a handle to a remote filesystem on one of the paths specified
    // by |kMountPoints|.
    zx_status_t InstallFs(const char* path, zx::channel h);

    // Serves connection to the root directory ("/") on |server|.
    zx_status_t ServeRoot(zx::channel server);

    // Serves connection to the fshost directory (exporting the "fuchsia.fshost" services) on
    // |server|.
    zx_status_t ServeFshostRoot(zx::channel server) {
        return registry_.ServeRoot(std::move(server));
    }

    // Triggers unmount when the FSHOST_SIGNAL_EXIT signal is raised on |event_|.
    //
    // Sets FSHOST_SIGNAL_EXIT_DONE when unmounting is complete.
    void WatchExit();

private:
    FsManager(zx::event fshost_event);
    zx_status_t Initialize();

    zx_status_t ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server, uint32_t rights);

    // Convenience wrapper for |ServeVnode| with maximum default permissions.
    zx_status_t ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server) {
        return ServeVnode(vn, std::move(server), ZX_FS_RIGHTS);
    }

    zx_status_t LocalMountReadOnly(memfs::VnodeDir* parent, const char* name,
                                   fbl::RefPtr<memfs::VnodeDir>& subtree);

    // Event on which "FSHOST_SIGNAL_XXX" signals are set.
    // Communicates state changes to/from devmgr.
    zx::event event_;

    static constexpr const char* kMountPoints[] = {"/bin",     "/data", "/volume", "/system",
                                                   "/install", "/blob", "/pkgfs"};
    fbl::RefPtr<fs::Vnode> mount_nodes[fbl::count_of(kMountPoints)];

    // The Root VFS manages the following filesystems:
    // - The global root filesystem (including the mount points)
    // - "/tmp"
    memfs::Vfs root_vfs_;

    // The System VFS manages exclusively the system filesystem.
    memfs::Vfs system_vfs_;
    fbl::unique_ptr<async::Loop> global_loop_;
    async::Wait global_shutdown_;

    // The base, root directory which serves the rest of the fshost.
    fbl::RefPtr<memfs::VnodeDir> global_root_;

    // Controls the external fshost vnode, as well as registration of filesystems
    // dynamically within the fshost.
    fshost::Registry registry_;
};

// Monitors "/dev/class/block" for new devices indefinitely.
void BlockDeviceWatcher(fbl::unique_ptr<FsManager> fshost, zx::unowned_job job, bool netboot);

} // namespace devmgr
