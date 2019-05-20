// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <fs/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

namespace {

constexpr size_t kPageSize = static_cast<size_t>(PAGE_SIZE);

}

namespace memfs {

zx_status_t Vfs::CreateFromVmo(VnodeDir* parent, fbl::StringPiece name,
                               zx_handle_t vmo, zx_off_t off,
                               zx_off_t len) {
    fbl::AutoLock lock(&vfs_lock_);
    return parent->CreateFromVmo(name, vmo, off, len);
}

void Vfs::MountSubtree(VnodeDir* parent, fbl::RefPtr<VnodeDir> subtree) {
    fbl::AutoLock lock(&vfs_lock_);
    parent->MountSubtree(std::move(subtree));
}

zx_status_t Vfs::FillFsId() {
    if (fs_id_) {
        return ZX_OK;
    }
    zx::event event;
    zx_status_t status = zx::event::create(0, &event);
    if (status != ZX_OK) {
        return status;
    }
    zx_info_handle_basic_t info;
    status = event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    fs_id_ = info.koid;
    return ZX_OK;
}

zx_status_t Vfs::GrowVMO(zx::vmo& vmo, size_t current_size,
                         size_t request_size, size_t* actual_size) {
    if (request_size <= current_size) {
        *actual_size = current_size;
        return ZX_OK;
    }
    size_t aligned_len = fbl::round_up(request_size, kPageSize);
    ZX_DEBUG_ASSERT(current_size % kPageSize == 0);
    size_t num_new_pages = (aligned_len - current_size) / kPageSize;
    if (num_new_pages + num_allocated_pages_ > pages_limit_) {
        *actual_size = current_size;
        return ZX_ERR_NO_SPACE;
    }
    zx_status_t status;
    if (!vmo.is_valid()) {
        if ((status = zx::vmo::create(aligned_len, ZX_VMO_RESIZABLE, &vmo)) != ZX_OK) {
            return status;
        }
    } else {
        if ((status = vmo.set_size(aligned_len)) != ZX_OK) {
            return status;
        }
    }
    // vmo operation succeeded
    num_allocated_pages_ += num_new_pages;
    *actual_size = aligned_len;
    return ZX_OK;
}

void Vfs::WillFreeVMO(size_t vmo_size) {
    ZX_DEBUG_ASSERT(vmo_size % kPageSize == 0);
    size_t freed_pages = vmo_size / kPageSize;
    ZX_DEBUG_ASSERT(freed_pages <= num_allocated_pages_);
    num_allocated_pages_ -= freed_pages;
}

std::atomic<uint64_t> VnodeMemfs::ino_ctr_ = 0;
std::atomic<uint64_t> VnodeMemfs::deleted_ino_ctr_ = 0;

VnodeMemfs::VnodeMemfs(Vfs* vfs) : dnode_(nullptr), link_count_(0), vfs_(vfs),
    ino_(ino_ctr_.fetch_add(1, std::memory_order_relaxed)) {
    zx_time_t now = 0;
    zx_clock_get_new(ZX_CLOCK_UTC, &now);
    create_time_ = modify_time_ = now;
}

VnodeMemfs::~VnodeMemfs() {
    deleted_ino_ctr_.fetch_add(1, std::memory_order_relaxed);
}

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

zx_status_t VnodeMemfs::AttachRemote(fs::MountChannel h) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    SetRemote(h.TakeChannel());
    return ZX_OK;
}

zx_status_t CreateFilesystem(const char* name, memfs::Vfs* vfs, fbl::RefPtr<VnodeDir>* out) {
    zx_status_t status;
    if ((status = vfs->FillFsId()) != ZX_OK) {
        return status;
    }
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
