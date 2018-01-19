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

#include <blobstore/blobstore.h>

using digest::Digest;

namespace blobstore {

VnodeBlob::~VnodeBlob() {
    blobstore_->ReleaseBlob(this);
    if (blob_ != nullptr) {
        block_fifo_request_t request;
        request.txnid = blobstore_->TxnId();
        request.vmoid = vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        blobstore_->Txn(&request, 1);
    }
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

    return blobstore_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t VnodeBlob::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    TRACE_DURATION("blobstore", "VnodeBlob::Read", "len", len, "off", off);

    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }

    return ReadInternal(data, len, off, out_actual);
}

zx_status_t VnodeBlob::Write(const void* data, size_t len, size_t offset,
                             size_t* out_actual) {
    TRACE_DURATION("blobstore", "VnodeBlob::Write", "len", len, "off", offset);
    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }
    zx_status_t status = WriteInternal(data, len, out_actual);
    return status;
}

zx_status_t VnodeBlob::Append(const void* data, size_t len, size_t* out_end,
                              size_t* out_actual) {
    zx_status_t status = Write(data, len, bytes_written_, out_actual);
    *out_actual = bytes_written_;
    return status;
}

zx_status_t VnodeBlob::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    TRACE_DURATION("blobstore", "VnodeBlob::Lookup", "name", name);
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
    if ((status = blobstore_->LookupBlob(digest, &vn)) < 0) {
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
    a->blksize = kBlobstoreBlockSize;
    a->blkcount = blobstore_->GetNode(map_index_)->num_blocks *
                  (kBlobstoreBlockSize / VNATTR_BLKSIZE);
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return ZX_OK;
}

zx_status_t VnodeBlob::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    TRACE_DURATION("blobstore", "VnodeBlob::Create", "name", name, "mode", mode);
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
    if ((status = blobstore_->NewBlob(digest, &vn)) != ZX_OK) {
        return status;
    }
    *out = fbl::move(vn);
    return ZX_OK;
}

constexpr const char kFsName[] = "blobstore";

zx_status_t VnodeBlob::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_VFS_QUERY_FS: {
        if (out_len < sizeof(vfs_query_info_t) + strlen(kFsName)) {
            return ZX_ERR_INVALID_ARGS;
        }
        vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
        memset(info, 0, sizeof(*info));
        info->block_size = kBlobstoreBlockSize;
        info->max_filename_size = Digest::kLength * 2;
        info->fs_type = VFS_TYPE_BLOBSTORE;
        info->fs_id = blobstore_->GetFsId();
        info->total_bytes = blobstore_->info_.block_count * blobstore_->info_.block_size;
        info->used_bytes = blobstore_->info_.alloc_block_count * blobstore_->info_.block_size;
        info->total_nodes = blobstore_->info_.inode_count;
        info->used_nodes = blobstore_->info_.alloc_inode_count;
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
        return blobstore_->Unmount();
    }
#ifdef __Fuchsia__
    case IOCTL_VFS_GET_DEVICE_PATH: {
        ssize_t len = ioctl_device_get_topo_path(blobstore_->Fd(), static_cast<char*>(out_buf), out_len);

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
    TRACE_DURATION("blobstore", "VnodeBlob::Truncate", "len", len);

    if (IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return SpaceAllocate(len);
}

zx_status_t VnodeBlob::Unlink(fbl::StringPiece name, bool must_be_dir) {
    TRACE_DURATION("blobstore", "VnodeBlob::Unlink", "name", name, "must_be_dir", must_be_dir);
    assert(memchr(name.data(), '/', name.length()) == nullptr);

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    Digest digest;
    fbl::RefPtr<VnodeBlob> out;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    } else if ((status = blobstore_->LookupBlob(digest, &out)) < 0) {
        return status;
    }
    out->QueueUnlink();
    return ZX_OK;
}

zx_status_t VnodeBlob::Mmap(int flags, size_t len, size_t* off, zx_handle_t* out) {
    TRACE_DURATION("blobstore", "VnodeBlob::Mmap", "flags", flags, "len", len, "off", off);

    if (IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (flags & FDIO_MMAP_FLAG_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_rights_t rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP;
    rights |= (flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
    rights |= (flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
    return CopyVmo(rights, out);
}

void VnodeBlob::Sync(SyncCallback closure) {
    // TODO(smklein): For now, this is a no-op, but it will change
    // once the kBlobFlagSync flag is in use.
    closure(ZX_OK);
}

} // namespace blobstore
