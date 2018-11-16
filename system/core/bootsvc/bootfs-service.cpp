// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs-service.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <utility>

#include <fs/connection.h>
#include <lib/bootfs/parser.h>
#include <zircon/compiler.h>

namespace bootsvc {

namespace {

// Adds the given vmo's range to the given path (potentially several elemenets) under a directory.
// The vmo must not be closed until the file is removed.
zx_status_t AddVmoFile(fbl::RefPtr<memfs::VnodeDir> vnb, const char* path,
                       const zx::vmo& vmo, zx_off_t off, size_t len) {
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
                                             vmo.get(), off, len);
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
            vnb = fbl::RefPtr<memfs::VnodeDir>::Downcast(std::move(out));
            path = nextpath + 1;
        }
    }
}

} // namespace

zx_status_t BootfsService::Create(zx::vmo bootfs_vmo, async_dispatcher_t* dispatcher,
                                  fbl::RefPtr<BootfsService>* out) {
    auto svc = fbl::AdoptRef(new BootfsService());

    bootfs::Parser parser;
    zx_status_t status = parser.Init(zx::unowned_vmo(bootfs_vmo));
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<memfs::VnodeDir> root;
    status = memfs::CreateFilesystem("<root>", &svc->vfs_, &root);
    if (status != ZX_OK) {
        return status;
    }

    // Load all of the entries in the bootfs into the FS
    status = parser.Parse([&root, &bootfs_vmo](const bootfs_entry_t *entry) -> zx_status_t {
        AddVmoFile(root, entry->name, bootfs_vmo, entry->data_off, entry->data_len);
        return ZX_OK;
    });
    if (status != ZX_OK) {
        return status;
    }

    svc->vfs_.SetDispatcher(dispatcher);
    svc->bootfs_ = std::move(bootfs_vmo);
    svc->root_ = std::move(root);
    *out = std::move(svc);
    return ZX_OK;
}

zx_status_t BootfsService::CreateRootConnection(zx::channel* out) {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
        return status;
    }

    auto conn = fbl::make_unique<fs::Connection>(&vfs_, root_, std::move(local),
                                                 ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_READABLE);
    status = vfs_.ServeConnection(std::move(conn));
    if (status != ZX_OK) {
        return status;
    }
    *out = std::move(remote);
    return ZX_OK;
}

zx_status_t BootfsService::Open(const char* path, zx::vmo* vmo, size_t* size) {
    fbl::RefPtr<fs::Vnode> node;
    fbl::StringPiece path_out;
    zx_status_t status = vfs_.Open(root_, &node, path, &path_out,
                                   ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_NOREMOTE, 0);
    if (status != ZX_OK) {
        return status;
    }
    ZX_ASSERT(path_out.size() == 0);

    fuchsia_io_NodeInfo info;
    status = node->GetHandles(ZX_FS_RIGHT_READABLE, &info);
    if (status != ZX_OK) {
        return status;
    }

    if (info.tag != fuchsia_io_NodeInfoTag_vmofile) {
        return ZX_ERR_WRONG_TYPE;
    }
    ZX_ASSERT(info.vmofile.offset == 0);
    zx::vmo underlying_vmo(info.vmofile.vmo);

    *vmo = std::move(underlying_vmo);
    *size = info.vmofile.length;
    return ZX_OK;
}

BootfsService::~BootfsService() {
    auto callback = [vmo(std::move(bootfs_))](zx_status_t status) mutable {
        // Bootfs uses multiple Vnodes which share a reference to a single VMO.
        // Since the lifetime of the VMO is coupled with the BootfsService, all
        // connections to these Vnodes must be terminated (with Shutdown) before
        // we can safely close the VMO
        vmo.reset();
    };
    vfs_.Shutdown(std::move(callback));
}

} // namespace bootsvc
