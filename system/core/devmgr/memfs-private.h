// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace devmgr {

// FsManager owns multiple sub-filesystems, managing them within a top-level
// in-memory filesystem.
class FsManager {
public:
    FsManager();

    // Created a named VmoFile in "/boot". Ownership of |vmo| assumed global.
    zx_status_t BootfsAddFile(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);
    // Created a named VmoFile in "/system". Ownership of |vmo| assumed global.
    zx_status_t SystemfsAddFile(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);

    // Create "/system", and mount it within the global root.
    zx_status_t MountSystem();

    // Set the "/system" VFS filesystem to become readonly.
    void SystemfsSetReadonly(bool value);

    // Pins a handle to a remote filesystem on one of the paths specified
    // by |kMountPoints|.
    zx_status_t InstallFs(const char* path, zx_handle_t h);

    // Give a channel to a root directory, where it will begin serving requests.
    zx_status_t ConnectRoot(zx::channel server);

    // Create a new channel, and connect to the root directory.
    // Invokes |ConnectRoot| internally.
    zx_status_t ServeRoot(zx::channel* out);

    // Passes the an event to all managed VFSs.
    //
    // Triggers unmount when the FSHOST_SIGNAL_EXIT signal is raised.
    // Sets FSHOST_SIGNAL_EXIT_DONE when unmounting is complete.
    void WatchExit(const zx::event& event);

private:
    zx_status_t ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server);
    zx_status_t LocalMount(memfs::VnodeDir* parent, const char* name,
                           fbl::RefPtr<memfs::VnodeDir>& subtree);

    static constexpr const char* kMountPoints[] = {
        "/data", "/volume", "/system", "/install", "/blob", "/pkgfs"
    };
    fbl::RefPtr<fs::Vnode> mount_nodes[fbl::count_of(kMountPoints)];

    // The Root VFS manages the following filesytems:
    // - The global root filesystem (including the mount points)
    // - "/boot"
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
    // The inflated "bootfs" filesystem, containing read-only packed VMOs.
    fbl::RefPtr<memfs::VnodeDir> bootfs_root_;
    // The location of an optional system image filesystem.
    fbl::RefPtr<memfs::VnodeDir> systemfs_root_;
};

void devmgr_vfs_exit();

} // namespace devmgr
