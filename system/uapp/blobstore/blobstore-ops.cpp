// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <magenta/device/vfs.h>

#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#include <mxio/debug.h>
#include <mxtl/ref_ptr.h>

#define MXDEBUG 0

#include "blobstore-private.h"

namespace blobstore {

VnodeBlob::~VnodeBlob() {
    blobstore_->ReleaseBlob(this);
}

mx_status_t VnodeBlob::Open(uint32_t flags) {
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return ERR_NOT_DIR;
    }
    return NO_ERROR;
}

mx_status_t VnodeBlob::Readdir(void* cookie, void* dirents, size_t len) {
    if (!IsDirectory()) {
        return ERR_NOT_DIR;
    }

    return blobstore_->Readdir(cookie, dirents, len);
}

ssize_t VnodeBlob::Read(void* data, size_t len, size_t off) {
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }

    size_t actual;
    mx_status_t status = ReadInternal(data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    return actual;
}

ssize_t VnodeBlob::Write(const void* data, size_t len, size_t off) {
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }
    size_t actual;
    mx_status_t status = WriteInternal(data, len, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    return actual;
}

mx_status_t VnodeBlob::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    assert(memchr(name, '/', len) == nullptr);
    if ((len == 1) && (name[0] == '.') && IsDirectory()) {
        // Special case: Accessing root directory via '.'
        *out = mxtl::RefPtr<VnodeBlob>(this);
        return NO_ERROR;
    }

    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    merkle::Digest digest;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    }
    mxtl::RefPtr<VnodeBlob> vn;
    if ((status = blobstore_->LookupBlob(digest, &vn)) < 0) {
        return status;
    }
    *out = mxtl::move(vn);
    return NO_ERROR;
}

mx_status_t VnodeBlob::Getattr(vnattr_t* a) {
    a->mode = IsDirectory() ? V_TYPE_DIR : V_TYPE_FILE;
    a->inode = 0;
    a->size = IsDirectory() ? 0 : SizeData();
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return NO_ERROR;
}

mx_status_t VnodeBlob::Create(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len, uint32_t mode) {
    assert(memchr(name, '/', len) == nullptr);
    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }

    merkle::Digest digest;
    mx_status_t status;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    }
    mxtl::RefPtr<VnodeBlob> vn;
    if ((status = blobstore_->NewBlob(digest, &vn)) != NO_ERROR) {
        return status;
    }
    *out = mxtl::move(vn);
    return NO_ERROR;
}

constexpr const char kFsName[] = "blobstore";

ssize_t VnodeBlob::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len) {
    switch (op) {
        case IOCTL_VFS_QUERY_FS: {
            if (out_len < strlen(kFsName) + 1) {
                return ERR_INVALID_ARGS;
            }
            strcpy(static_cast<char*>(out_buf), kFsName);
            return strlen(kFsName);
        }
        case IOCTL_VFS_UNMOUNT_FS: {
            mx_status_t status = Sync();
            if (status != NO_ERROR) {
                error("blobstore unmount failed to sync; unmounting anyway: %d\n", status);
            }
            return blobstore_->Unmount();
        }
        default: {
            return ERR_NOT_SUPPORTED;
        }
    }
}

mx_status_t VnodeBlob::Truncate(size_t len) {
    if (IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }

    return SpaceAllocate(len);
}

mx_status_t VnodeBlob::Unlink(const char* name, size_t len, bool must_be_dir) {
    assert(memchr(name, '/', len) == nullptr);
    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    merkle::Digest digest;
    mxtl::RefPtr<VnodeBlob> out;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    } else if ((status = blobstore_->LookupBlob(digest, &out)) < 0) {
        return status;
    }
    out->QueueUnlink();
    return NO_ERROR;
}

mx_status_t VnodeBlob::Mmap(int flags, size_t len, size_t* off, mx_handle_t* out) {
    if (IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }
    if (flags & MXIO_MMAP_FLAG_WRITE) {
        return ERR_NOT_SUPPORTED;
    }

    mx_rights_t rights = MX_RIGHT_TRANSFER | MX_RIGHT_MAP;
    rights |= (flags & MXIO_MMAP_FLAG_READ) ? MX_RIGHT_READ : 0;
    rights |= (flags & MXIO_MMAP_FLAG_EXEC) ? MX_RIGHT_EXECUTE : 0;
    return CopyVmo(rights, out);
}

mx_status_t VnodeBlob::Sync() {
    // TODO(smklein): For now, this is a no-op, but it will change
    // once the kBlobFlagSync flag is in use.
    return NO_ERROR;
}

} // namespace blobstore
