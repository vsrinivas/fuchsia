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

#include <magenta/device/device.h>
#include <magenta/device/vfs.h>

#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/vfs.h>
#include <fbl/ref_ptr.h>

#define MXDEBUG 0

#include "blobstore-private.h"

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

mx_status_t VnodeBlob::Open(uint32_t flags) {
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return MX_ERR_NOT_DIR;
    } else if (IsDirectory() && ((flags & O_ACCMODE) != 0)) {
        return MX_ERR_NOT_FILE;
    }

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
    case O_RDWR:
        if (GetState() != kBlobStateEmpty) {
            return MX_ERR_ACCESS_DENIED;
        }
    }
    return MX_OK;
}

mx_status_t VnodeBlob::Readdir(void* cookie, void* dirents, size_t len) {
    if (!IsDirectory()) {
        return MX_ERR_NOT_DIR;
    }

    return blobstore_->Readdir(cookie, dirents, len);
}

ssize_t VnodeBlob::Read(void* data, size_t len, size_t off) {
    if (IsDirectory()) {
        return MX_ERR_NOT_FILE;
    }

    size_t actual;
    mx_status_t status = ReadInternal(data, len, off, &actual);
    if (status != MX_OK) {
        return status;
    }
    return actual;
}

ssize_t VnodeBlob::Write(const void* data, size_t len, size_t off) {
    if (IsDirectory()) {
        return MX_ERR_NOT_FILE;
    }
    size_t actual;
    mx_status_t status = WriteInternal(data, len, &actual);
    if (status != MX_OK) {
        return status;
    }
    return actual;
}

mx_status_t VnodeBlob::Lookup(fbl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    assert(memchr(name, '/', len) == nullptr);
    if ((len == 1) && (name[0] == '.') && IsDirectory()) {
        // Special case: Accessing root directory via '.'
        *out = fbl::RefPtr<VnodeBlob>(this);
        return MX_OK;
    }

    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    Digest digest;
    if ((status = digest.Parse(name, len)) != MX_OK) {
        return status;
    }
    fbl::RefPtr<VnodeBlob> vn;
    if ((status = blobstore_->LookupBlob(digest, &vn)) < 0) {
        return status;
    }
    *out = fbl::move(vn);
    return MX_OK;
}

mx_status_t VnodeBlob::Getattr(vnattr_t* a) {
    a->mode = IsDirectory() ? V_TYPE_DIR : V_TYPE_FILE;
    a->inode = 0;
    a->size = IsDirectory() ? 0 : SizeData();
    a->blksize = kBlobstoreBlockSize;
    a->blkcount = blobstore_->GetNode(map_index_)->num_blocks *
                  (kBlobstoreBlockSize / VNATTR_BLKSIZE);
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return MX_OK;
}

mx_status_t VnodeBlob::Create(fbl::RefPtr<fs::Vnode>* out, const char* name, size_t len, uint32_t mode) {
    assert(memchr(name, '/', len) == nullptr);
    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }

    Digest digest;
    mx_status_t status;
    if ((status = digest.Parse(name, len)) != MX_OK) {
        return status;
    }
    fbl::RefPtr<VnodeBlob> vn;
    if ((status = blobstore_->NewBlob(digest, &vn)) != MX_OK) {
        return status;
    }
    *out = fbl::move(vn);
    return MX_OK;
}

constexpr const char kFsName[] = "blobstore";

ssize_t VnodeBlob::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len) {
    switch (op) {
    case IOCTL_VFS_QUERY_FS: {
        if (out_len < sizeof(vfs_query_info_t) + strlen(kFsName)) {
            return MX_ERR_INVALID_ARGS;
        }
        vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
        info->total_bytes = blobstore_->info_.block_count * blobstore_->info_.block_size;
        info->used_bytes = blobstore_->info_.alloc_block_count * blobstore_->info_.block_size;
        info->total_nodes = blobstore_->info_.inode_count;
        info->used_nodes = blobstore_->info_.alloc_inode_count;
        memcpy(info->name, kFsName, strlen(kFsName));
        return sizeof(vfs_query_info_t) + strlen(kFsName);
    }
    case IOCTL_VFS_UNMOUNT_FS: {
        mx_status_t status = Sync();
        if (status != MX_OK) {
            FS_TRACE_ERROR("blobstore unmount failed to sync; unmounting anyway: %d\n", status);
        }
        return blobstore_->Unmount();
    }
#ifdef __Fuchsia__
    case IOCTL_VFS_GET_DEVICE_PATH: {
        ssize_t len = ioctl_device_get_topo_path(blobstore_->blockfd_, static_cast<char*>(out_buf), out_len);

        if ((ssize_t)out_len < len) {
            return MX_ERR_INVALID_ARGS;
        }

        return len;
    }
#endif
    default: {
        return MX_ERR_NOT_SUPPORTED;
    }
    }
}

mx_status_t VnodeBlob::Truncate(size_t len) {
    if (IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }

    return SpaceAllocate(len);
}

mx_status_t VnodeBlob::Unlink(const char* name, size_t len, bool must_be_dir) {
    assert(memchr(name, '/', len) == nullptr);
    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    Digest digest;
    fbl::RefPtr<VnodeBlob> out;
    if ((status = digest.Parse(name, len)) != MX_OK) {
        return status;
    } else if ((status = blobstore_->LookupBlob(digest, &out)) < 0) {
        return status;
    }
    out->QueueUnlink();
    return MX_OK;
}

mx_status_t VnodeBlob::Mmap(int flags, size_t len, size_t* off, mx_handle_t* out) {
    if (IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }
    if (flags & MXIO_MMAP_FLAG_WRITE) {
        return MX_ERR_NOT_SUPPORTED;
    }

    mx_rights_t rights = MX_RIGHT_TRANSFER | MX_RIGHT_MAP;
    rights |= (flags & MXIO_MMAP_FLAG_READ) ? MX_RIGHT_READ : 0;
    rights |= (flags & MXIO_MMAP_FLAG_EXEC) ? MX_RIGHT_EXECUTE : 0;
    return CopyVmo(rights, out);
}

mx_status_t VnodeBlob::Sync() {
    // TODO(smklein): For now, this is a no-op, but it will change
    // once the kBlobFlagSync flag is in use.
    return MX_OK;
}

} // namespace blobstore
