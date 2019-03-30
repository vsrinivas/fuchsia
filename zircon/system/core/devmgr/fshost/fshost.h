// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "../shared/env.h"
#include "../shared/fdio.h"

namespace devmgr {

// FshostConnections represents the link from fshost to external
// sources outside fshost, such as the devmgr.
class FshostConnections {
public:
    FshostConnections(zx::channel devfs_root, zx::channel svc_root, zx::channel fs_root,
                      zx::event event);

    // Synchronously opens a connection on the requested path.
    zx_status_t Open(const char* path, zx::channel* out_connection) const;

    // Create and install the namespace for the current process, using
    // the owned channels as connections.
    zx_status_t CreateNamespace();

    const zx::event& Event() const { return event_; }

private:
    zx::channel devfs_root_;
    zx::channel svc_root_;
    zx::channel fs_root_;
    zx::event event_;
};

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
public:
    FsManager();

    const FshostConnections& GetConnections() const { return *connections_.get(); }

    // Created a named VmoFile in "/system". Ownership of |vmo| assumed global.
    zx_status_t SystemfsAddFile(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);

    // Signal that "/system" has been mounted.
    void FuchsiaStart() const { connections_->Event().signal(0, FSHOST_SIGNAL_READY); }

    // Create "/system", and mount it within the global root.
    zx_status_t MountSystem();

    // Identifies if "/system" has already been mounted.
    bool IsSystemMounted() const { return systemfs_root_ != nullptr; }

    // Set the "/system" VFS filesystem to become readonly.
    void SystemfsSetReadonly(bool value);

    // Pins a handle to a remote filesystem on one of the paths specified
    // by |kMountPoints|.
    zx_status_t InstallFs(const char* path, zx::channel h);

    // Initialize connections to external service managers, and begin
    // monitoring |event| for a termination event.
    zx_status_t InitializeConnections(zx::channel root, zx::channel devfs_root,
                                      zx::channel svc_root, zx::event event);

private:
    // Triggers unmount when the FSHOST_SIGNAL_EXIT signal is raised on an
    // event contained within |connections_|.
    //
    // Sets FSHOST_SIGNAL_EXIT_DONE when unmounting is complete.
    void WatchExit();

    // Give a channel to a root directory, where it will begin serving requests.
    zx_status_t ConnectRoot(zx::channel server);

    // Create a new channel, and connect to the root directory.
    // Invokes |ConnectRoot| internally.
    zx_status_t ServeRoot(zx::channel* out);

    zx_status_t ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server, uint32_t rights);

    // Convenience wrapper for |ServeVnode| with maximum default permissions.
    zx_status_t ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server) {
        return ServeVnode(vn, std::move(server), ZX_FS_RIGHTS);
    }

    zx_status_t LocalMountReadOnly(memfs::VnodeDir* parent, const char* name,
                                   fbl::RefPtr<memfs::VnodeDir>& subtree);

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
    // The globally accessible "/tmp", in-memory filesystem directory.
    fbl::RefPtr<memfs::VnodeDir> memfs_root_;

    // The location of an optional system image filesystem.
    fbl::RefPtr<memfs::VnodeDir> systemfs_root_;

    // Allows access and signals to external resources.
    fbl::unique_ptr<FshostConnections> connections_;
};

// Function which mounts a handle on behalf of the block watcher.
void block_device_watcher(fbl::unique_ptr<FsManager> fshost, zx::unowned_job job, bool netboot);

} // namespace devmgr
