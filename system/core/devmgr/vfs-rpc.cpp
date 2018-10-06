// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <fs/vfs.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "devmgr.h"
#include "fshost.h"

#define ZXDEBUG 0

namespace devmgr {
namespace {

zx_status_t AddVmofile(fbl::RefPtr<memfs::VnodeDir> vnb, const char* path, zx_handle_t vmo,
                       zx_off_t off, size_t len) {
    zx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ZX_ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == nullptr) {
            if (path[0] == 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            return vnb->vfs()->CreateFromVmo(vnb.get(), fbl::StringPiece(path, strlen(path)),
                                             vmo, off, len);
        } else {
            if (nextpath == path) {
                return ZX_ERR_INVALID_ARGS;
            }

            fbl::RefPtr<fs::Vnode> out;
            r = vnb->Lookup(&out, fbl::StringPiece(path, nextpath - path));
            if (r == ZX_ERR_NOT_FOUND) {
                r = vnb->Create(&out, fbl::StringPiece(path, nextpath - path), S_IFDIR);
            }

            if (r < 0) {
                return r;
            }
            vnb = fbl::RefPtr<memfs::VnodeDir>::Downcast(fbl::move(out));
            path = nextpath + 1;
        }
    }
}

} // namespace

// TODO: For operations which can fail, we should use a private constructor
// pattern and create FsManager with error validation prior to calling
// the real constructor.
FsManager::FsManager() {
    ZX_ASSERT(global_root_ == nullptr);
    zx_status_t status = CreateFilesystem("<root>", &root_vfs_, &global_root_);
    ZX_ASSERT(status == ZX_OK);

    status = CreateFilesystem("boot", &root_vfs_, &bootfs_root_);
    ZX_ASSERT(status == ZX_OK);
    root_vfs_.MountSubtree(global_root_.get(), bootfs_root_);

    status = CreateFilesystem("tmp", &root_vfs_, &memfs_root_);
    ZX_ASSERT(status == ZX_OK);
    root_vfs_.MountSubtree(global_root_.get(), memfs_root_);

    for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
        fbl::StringPiece pathout;
        status = root_vfs_.Open(global_root_, &mount_nodes[n],
                                fbl::StringPiece(kMountPoints[n]), &pathout,
                                ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_CREATE, S_IFDIR);
        ZX_ASSERT(status == ZX_OK);
    }

    global_loop_.reset(new async::Loop(&kAsyncLoopConfigNoAttachToThread));
    global_loop_->StartThread("root-dispatcher");
    root_vfs_.SetDispatcher(global_loop_->dispatcher());
    system_vfs_.SetDispatcher(global_loop_->dispatcher());
}

zx_status_t FsManager::BootfsAddFile(const char* path, zx_handle_t vmo, zx_off_t off,
                                     size_t len) {
    return AddVmofile(bootfs_root_, path, vmo, off, len);
}

zx_status_t FsManager::SystemfsAddFile(const char* path, zx_handle_t vmo, zx_off_t off,
                                       size_t len) {
    return AddVmofile(systemfs_root_, path, vmo, off, len);
}

zx_status_t FsManager::MountSystem() {
    ZX_ASSERT(systemfs_root_ == nullptr);
    zx_status_t status = CreateFilesystem("system", &system_vfs_, &systemfs_root_);
    ZX_ASSERT(status == ZX_OK);
    return LocalMount(global_root_.get(), "system", systemfs_root_);
}

void FsManager::SystemfsSetReadonly(bool value) {
    ZX_ASSERT(systemfs_root_ == nullptr);
    systemfs_root_->vfs()->SetReadonly(value);
}

zx_status_t FsManager::InstallFs(const char* path, zx::channel h) {
    for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
        if (!strcmp(path, kMountPoints[n])) {
            return root_vfs_.InstallRemote(mount_nodes[n], fs::MountChannel(fbl::move(h)));
        }
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t FsManager::InitializeConnections(zx::channel root, zx::channel devfs_root,
                                             zx::channel svc_root, zx::event fshost_event) {
    // Serve devmgr's root handle using our own root directory.
    zx_status_t status = ConnectRoot(fbl::move(root));
    if (status != ZX_OK) {
        printf("fshost: Cannot connect to fshost root: %d\n", status);
    }

    zx::channel fs_root;
    if ((status = ServeRoot(&fs_root)) != ZX_OK) {
        printf("fshost: cannot create global root\n");
    }

    connections_ = fbl::make_unique<FshostConnections>(fbl::move(devfs_root),
                                                       fbl::move(svc_root),
                                                       fbl::move(fs_root),
                                                       fbl::move(fshost_event));
    // Now that we've initialized our connection to the outside world,
    // monitor for external shutdown events.
    WatchExit();
    return connections_->CreateNamespace();
}

zx_status_t FsManager::ConnectRoot(zx::channel server) {
    return ServeVnode(global_root_, fbl::move(server));
}

zx_status_t FsManager::ServeRoot(zx::channel* out) {
    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
        return ZX_OK;
    }
    if ((status = ServeVnode(global_root_, fbl::move(server))) != ZX_OK) {
        return status;
    }
    *out = fbl::move(client);
    return ZX_OK;
}

void FsManager::WatchExit() {
    global_shutdown_.set_handler([this](async_dispatcher_t* dispatcher,
                                        async::Wait* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
        root_vfs_.UninstallAll(ZX_TIME_INFINITE);
        system_vfs_.UninstallAll(ZX_TIME_INFINITE);
        connections_->Event().signal(0, FSHOST_SIGNAL_EXIT_DONE);
    });

    global_shutdown_.set_object(connections_->Event().get());
    global_shutdown_.set_trigger(FSHOST_SIGNAL_EXIT);
    global_shutdown_.Begin(global_loop_->dispatcher());
}

zx_status_t FsManager::ServeVnode(fbl::RefPtr<memfs::VnodeDir>& vn, zx::channel server) {
    return vn->vfs()->ServeDirectory(vn, fbl::move(server));
}

zx_status_t FsManager::LocalMount(memfs::VnodeDir* parent, const char* name,
                                  fbl::RefPtr<memfs::VnodeDir>& subtree) {
    fbl::RefPtr<fs::Vnode> vn;
    zx_status_t status = parent->Lookup(&vn, fbl::StringPiece(name));
    if (status != ZX_OK) {
        return status;
    }
    zx::channel client, server;
    status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
        return ZX_OK;
    }
    if ((status = ServeVnode(subtree, fbl::move(server))) != ZX_OK) {
        return status;
    }
    return parent->vfs()->InstallRemote(fbl::move(vn),
                                        fs::MountChannel(fbl::move(client)));
}

} // namespace devmgr
