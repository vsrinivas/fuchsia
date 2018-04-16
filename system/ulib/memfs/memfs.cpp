// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <lib/async-loop/cpp/loop.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fdio/namespace.h>
#include <fdio/vfs.h>
#include <fs/vfs.h>
#include <memfs/memfs.h>
#include <memfs/vnode.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

namespace memfs {

zx_status_t Vfs::CreateFromVmo(VnodeDir* parent, bool vmofile, fbl::StringPiece name,
                             zx_handle_t vmo, zx_off_t off,
                             zx_off_t len) {
    fbl::AutoLock lock(&vfs_lock_);
    return parent->CreateFromVmo(vmofile, name, vmo, off, len);
}

void Vfs::MountSubtree(VnodeDir* parent, fbl::RefPtr<VnodeDir> subtree) {
    fbl::AutoLock lock(&vfs_lock_);
    parent->MountSubtree(fbl::move(subtree));
}

fbl::atomic<uint64_t> VnodeMemfs::ino_ctr_(0);

VnodeMemfs::VnodeMemfs(Vfs* vfs) : dnode_(nullptr), link_count_(0), vfs_(vfs),
    ino_(ino_ctr_.fetch_add(1, fbl::memory_order_relaxed)) {
    create_time_ = modify_time_ = zx_clock_get(ZX_CLOCK_UTC);
}

VnodeMemfs::~VnodeMemfs() {}

zx_status_t VnodeMemfs::Setattr(const vnattr_t* attr) {
    if ((attr->valid & ~(ATTR_MTIME)) != 0) {
        // only attr currently supported
        return ZX_ERR_INVALID_ARGS;
    }
    if (attr->valid & ATTR_MTIME) {
        modify_time_ = attr->modify_time;
    }
    return ZX_OK;
}

void VnodeMemfs::Sync(SyncCallback closure) {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    closure(ZX_OK);
}

constexpr const char kFsName[] = "memfs";

zx_status_t VnodeMemfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                              void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_VFS_QUERY_FS: {
        if (out_len < sizeof(vfs_query_info_t) + strlen(kFsName)) {
            return ZX_ERR_INVALID_ARGS;
        }

        vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
        memset(info, 0, sizeof(*info));
        //TODO(planders): eventually report something besides 0.
        memcpy(info->name, kFsName, strlen(kFsName));
        *out_actual = sizeof(vfs_query_info_t) + strlen(kFsName);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t VnodeMemfs::AttachRemote(fs::MountChannel h) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    SetRemote(fbl::move(h.TakeChannel()));
    return ZX_OK;
}

zx_status_t createFilesystem(const char* name, memfs::Vfs* vfs, fbl::RefPtr<VnodeDir>* out) {
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeDir> fs = fbl::AdoptRef(new (&ac) VnodeDir(vfs));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::RefPtr<Dnode> dn = Dnode::Create(name, fs);
    if (dn == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    fs->dnode_ = dn; // FS root is directory
    *out = fs;
    return ZX_OK;
}

} // namespace memfs

struct memfs_filesystem {
    memfs::Vfs vfs;
};

zx_status_t memfs_create_filesystem(async_t* async, memfs_filesystem_t** fs_out,
                                    zx_handle_t* root_out) {
    ZX_DEBUG_ASSERT(async != nullptr);
    ZX_DEBUG_ASSERT(fs_out != nullptr);
    ZX_DEBUG_ASSERT(root_out != nullptr);

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<memfs_filesystem_t> fs = fbl::make_unique<memfs_filesystem_t>();
    fs->vfs.set_async(async);

    fbl::RefPtr<memfs::VnodeDir> root;
    if ((status = memfs::createFilesystem("<tmp>", &fs->vfs, &root)) != ZX_OK) {
        return status;
    }
    if ((status = fs->vfs.ServeDirectory(fbl::move(root), fbl::move(server))) != ZX_OK) {
        return status;
    }

    *fs_out = fs.release();
    *root_out = client.release();
    return ZX_OK;
}

zx_status_t memfs_install_at(async_t* async, const char* path) {
    fdio_ns_t* ns;
    zx_status_t status = fdio_ns_get_installed(&ns);
    if (status != ZX_OK) {
        return status;
    }

    memfs_filesystem_t* fs;
    zx_handle_t root;
    status = memfs_create_filesystem(async, &fs, &root);
    if (status != ZX_OK) {
        return status;
    }

    status = fdio_ns_bind(ns, path, root);
    if (status != ZX_OK) {
        memfs_free_filesystem(fs, 0);
        zx_handle_close(root);
        return status;
    }

    // We leak |fs| because there's no way to spin down the file system.
    // See ZX-1576 for more details.
    return ZX_OK;
}

zx_status_t memfs_free_filesystem(memfs_filesystem_t* fs, zx_duration_t timeout) {
    ZX_DEBUG_ASSERT(fs != nullptr);
    zx_status_t status = fs->vfs.UninstallAll(zx_deadline_after(timeout));
    delete fs;
    return status;
}
