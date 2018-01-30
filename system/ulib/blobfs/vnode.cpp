// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>

#include <fbl/ref_ptr.h>
#include <fdio/debug.h>
#include <fdio/vfs.h>
#include <sync/completion.h>
#include <zircon/syscalls.h>

#define ZXDEBUG 0

#include <blobfs/blobfs.h>

using digest::Digest;

namespace blobfs {

void VnodeBlob::fbl_recycle() {
    if (GetState() != kBlobStatePurged) {
        // Relocate blobs which haven't been deleted to the closed cache.
        blobfs_->VnodeReleaseSoft(this);
    } else {
        // Destroy blobs which have been purged.
        delete this;
    }
}

void VnodeBlob::TearDown() {
    ZX_ASSERT(clone_watcher_.object() == ZX_HANDLE_INVALID);
    if (blob_ != nullptr) {
        block_fifo_request_t request;
        request.txnid = blobfs_->TxnId();
        request.vmoid = vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        blobfs_->Txn(&request, 1);
    }
    blob_ = nullptr;
}

VnodeBlob::~VnodeBlob() {
    TearDown();
}

zx_status_t VnodeBlob::ValidateFlags(uint32_t flags) {
    if ((flags & ZX_FS_FLAG_DIRECTORY) && !IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    }

    if (flags & ZX_FS_RIGHT_WRITABLE) {
        if (IsDirectory()) {
            return ZX_ERR_NOT_FILE;
        } else if (GetState() != kBlobStateEmpty) {
            return ZX_ERR_ACCESS_DENIED;
        }
    }
    return ZX_OK;
}

zx_status_t VnodeBlob::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                               size_t* out_actual) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    }

    return blobfs_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t VnodeBlob::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    TRACE_DURATION("blobfs", "VnodeBlob::Read", "len", len, "off", off);

    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }

    return ReadInternal(data, len, off, out_actual);
}

zx_status_t VnodeBlob::Write(const void* data, size_t len, size_t offset,
                             size_t* out_actual) {
    TRACE_DURATION("blobfs", "VnodeBlob::Write", "len", len, "off", offset);
    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }
    return WriteInternal(data, len, out_actual);
}

zx_status_t VnodeBlob::Append(const void* data, size_t len, size_t* out_end,
                              size_t* out_actual) {
    zx_status_t status = Write(data, len, bytes_written_, out_actual);
    *out_actual = bytes_written_;
    return status;
}

zx_status_t VnodeBlob::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    TRACE_DURATION("blobfs", "VnodeBlob::Lookup", "name", name);
    assert(memchr(name.data(), '/', name.length()) == nullptr);
    if (name == "." && IsDirectory()) {
        // Special case: Accessing root directory via '.'
        *out = fbl::RefPtr<VnodeBlob>(this);
        return ZX_OK;
    }

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    Digest digest;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    }
    fbl::RefPtr<VnodeBlob> vn;
    if ((status = blobfs_->LookupBlob(digest, &vn)) < 0) {
        return status;
    }
    *out = fbl::move(vn);
    return ZX_OK;
}

zx_status_t VnodeBlob::Getattr(vnattr_t* a) {
    memset(a, 0, sizeof(vnattr_t));
    a->mode = (IsDirectory() ? V_TYPE_DIR : V_TYPE_FILE) | V_IRUSR;
    a->inode = 0;
    a->size = IsDirectory() ? 0 : SizeData();
    a->blksize = kBlobfsBlockSize;
    a->blkcount = inode_.num_blocks * (kBlobfsBlockSize / VNATTR_BLKSIZE);
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return ZX_OK;
}

zx_status_t VnodeBlob::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    TRACE_DURATION("blobfs", "VnodeBlob::Create", "name", name, "mode", mode);
    assert(memchr(name.data(), '/', name.length()) == nullptr);

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    Digest digest;
    zx_status_t status;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    }
    fbl::RefPtr<VnodeBlob> vn;
    if ((status = blobfs_->NewBlob(digest, &vn)) != ZX_OK) {
        return status;
    }
    vn->fd_count_ = 1;
    *out = fbl::move(vn);
    return ZX_OK;
}

constexpr const char kFsName[] = "blobfs";

zx_status_t VnodeBlob::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_VFS_QUERY_FS: {
        if (out_len < sizeof(vfs_query_info_t) + strlen(kFsName)) {
            return ZX_ERR_INVALID_ARGS;
        }
        vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
        memset(info, 0, sizeof(*info));
        info->block_size = kBlobfsBlockSize;
        info->max_filename_size = Digest::kLength * 2;
        info->fs_type = VFS_TYPE_BLOBFS;
        info->fs_id = blobfs_->GetFsId();
        info->total_bytes = blobfs_->info_.block_count * blobfs_->info_.block_size;
        info->used_bytes = blobfs_->info_.alloc_block_count * blobfs_->info_.block_size;
        info->total_nodes = blobfs_->info_.inode_count;
        info->used_nodes = blobfs_->info_.alloc_inode_count;
        memcpy(info->name, kFsName, strlen(kFsName));
        *out_actual = sizeof(vfs_query_info_t) + strlen(kFsName);
        return ZX_OK;
    }
    case IOCTL_VFS_UNMOUNT_FS: {
        // TODO(ZX-1577): Avoid calling completion_wait here.
        // Prefer to use dispatcher's async_t to be notified
        // whenever Sync completes.
        completion_t completion;
        SyncCallback closure([&completion](zx_status_t status) {
            completion_signal(&completion);
        });
        Sync(fbl::move(closure));
        completion_wait(&completion, ZX_TIME_INFINITE);
        *out_actual = 0;
        return blobfs_->Unmount();
    }
#ifdef __Fuchsia__
    case IOCTL_VFS_GET_DEVICE_PATH: {
        ssize_t len = ioctl_device_get_topo_path(blobfs_->Fd(), static_cast<char*>(out_buf), out_len);

        if ((ssize_t)out_len < len) {
            return ZX_ERR_INVALID_ARGS;
        }

        if (len >= 0) {
            *out_actual = len;
        }
        return len > 0 ? ZX_OK : static_cast<zx_status_t>(len);
    }
#endif
    default: {
        return ZX_ERR_NOT_SUPPORTED;
    }
    }
}

zx_status_t VnodeBlob::Truncate(size_t len) {
    TRACE_DURATION("blobfs", "VnodeBlob::Truncate", "len", len);

    if (IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return SpaceAllocate(len);
}

zx_status_t VnodeBlob::Unlink(fbl::StringPiece name, bool must_be_dir) {
    TRACE_DURATION("blobfs", "VnodeBlob::Unlink", "name", name, "must_be_dir", must_be_dir);
    assert(memchr(name.data(), '/', name.length()) == nullptr);

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    Digest digest;
    fbl::RefPtr<VnodeBlob> out;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    } else if ((status = blobfs_->LookupBlob(digest, &out)) < 0) {
        return status;
    }
    out->QueueUnlink();
    return ZX_OK;
}

zx_status_t VnodeBlob::GetVmo(int flags, zx_handle_t* out) {
    TRACE_DURATION("blobfs", "VnodeBlob::GetVmo", "flags", flags);

    if (IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (flags & FDIO_MMAP_FLAG_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
    } else if (flags & FDIO_MMAP_FLAG_EXACT) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Let clients map and set the names of their VMOs.
    zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
    // We can ignore FDIO_MMAP_FLAG_PRIVATE, since private / shared access
    // to the underlying VMO can both be satisfied with a clone due to
    // the immutability of blobfs blobs.
    rights |= (flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
    rights |= (flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
    return CloneVmo(rights, out);
}

void VnodeBlob::Sync(SyncCallback closure) {
    if (atomic_load(&syncing_)) {
        blobfs_->Sync([this, cb = fbl::move(closure)](zx_status_t status) {
            if (status != ZX_OK) {
                cb(status);
                return;
            }

            status = fsync(blobfs_->Fd());
            cb(status);
        });
    } else {
        closure(ZX_OK);
    }
}

void VnodeBlob::CompleteSync() {
    fsync(blobfs_->Fd());
    atomic_store(&syncing_, false);
}

zx_status_t VnodeBlob::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    fd_count_++;
    return ZX_OK;
}

zx_status_t VnodeBlob::Close() {
    ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Closing blob with no fds open");
    fd_count_--;
    // Attempt purge in case blob was unlinked prior to close
    TryPurge();
    return ZX_OK;
}

void VnodeBlob::Purge() {
    ZX_DEBUG_ASSERT(fd_count_ == 0);
    ZX_DEBUG_ASSERT(Purgeable());
    blobfs_->PurgeBlob(this);
    SetState(kBlobStatePurged);
}

} // namespace blobfs
