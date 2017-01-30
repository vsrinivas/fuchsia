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

#include <magenta/device/devmgr.h>

#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#include <mxio/debug.h>

#define MXDEBUG 0

#include "blobstore-private.h"

#define VNODE_IS_DIR(vn) (vn->blob == nullptr)

namespace blobstore {

static void fs_release(vnode_t* vn) {
    vn->blobstore->ReleaseBlob(mxtl::move(vn->blob));
    free(vn);
}

static mx_status_t fs_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    if ((flags & O_DIRECTORY) && !VNODE_IS_DIR(vn)) {
        return ERR_NOT_DIR;
    }
    vn_acquire(vn);
    return NO_ERROR;
}

static mx_status_t fs_close(vnode_t* vn) {
    vn_release(vn);
    return NO_ERROR;
}

static ssize_t fs_read(vnode_t* vn, void* data, size_t len, size_t off) {
    if (VNODE_IS_DIR(vn)) {
        return ERR_NOT_FILE;
    }

    size_t actual;
    mx_status_t status = vn->blob->Read(data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    return actual;
}

static ssize_t fs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    if (VNODE_IS_DIR(vn)) {
        return ERR_NOT_FILE;
    }
    size_t actual;
    mx_status_t status = vn->blob->Write(data, len, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    return actual;
}

static mx_status_t fs_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    assert(memchr(name, '/', len) == nullptr);
    if ((len == 1) && (name[0] == '.') && VNODE_IS_DIR(vn)) {
        // Special case: Accessing root directory via '.'
        vn_acquire(vn);
        *out = vn;
        return NO_ERROR;
    }

    if (!VNODE_IS_DIR(vn)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    merkle::Digest digest;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    } else if ((status = Blobstore::LookupBlob(vn->blobstore, digest, out)) < 0) {
        return status;
    }
    return NO_ERROR;
}

static mx_status_t fs_getattr(vnode_t* vn, vnattr_t* a) {
    a->inode = 0;
    a->size = VNODE_IS_DIR(vn) ? 0 : vn->blob->SizeData();
    a->mode = VNODE_IS_DIR(vn) ? V_TYPE_DIR : V_TYPE_FILE;
    a->create_time = 0;
    a->modify_time = 0;
    return NO_ERROR;
}

static mx_status_t fs_setattr_none(vnode_t* vn, vnattr_t* a) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_readdir_none(vnode_t* vn, void* cookie, void* dirents, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_create(vnode_t* vn, vnode_t** out,
                             const char* name, size_t len, uint32_t mode) {
    assert(memchr(name, '/', len) == nullptr);
    if (!VNODE_IS_DIR(vn)) {
        return ERR_NOT_SUPPORTED;
    }

    merkle::Digest digest;
    mx_status_t status;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    }
    return Blobstore::NewBlob(vn->blobstore, digest, out);
}

static ssize_t fs_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                        size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
        case IOCTL_DEVMGR_UNMOUNT_FS: {
            mx_status_t status = vn->ops->sync(vn);
            if (status != NO_ERROR) {
                error("blobstore unmount failed to sync; unmounting anyway: %d\n", status);
            }
            return vn->blobstore->Unmount();
        }
        case IOCTL_BLOBSTORE_BLOB_INIT: {
            if (VNODE_IS_DIR(vn)) {
                return ERR_NOT_SUPPORTED;
            }

            if ((in_len != sizeof(blob_ioctl_config_t)) || (out_len != 0)) {
                return ERR_INVALID_ARGS;
            }
            const blob_ioctl_config_t* config = static_cast<const blob_ioctl_config_t*>(in_buf);
            return vn->blob->SpaceAllocate(config->size_data);
        }
        default: {
            return ERR_NOT_SUPPORTED;
        }
    }
}

static mx_status_t fs_truncate_none(vnode_t* vn, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_unlink(vnode_t* vn, const char* name, size_t len, bool must_be_dir) {
    assert(memchr(name, '/', len) == nullptr);
    if (!VNODE_IS_DIR(vn)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t status;
    merkle::Digest digest;
    vnode_t* out;
    if ((status = digest.Parse(name, len)) != NO_ERROR) {
        return status;
    } else if ((status = Blobstore::LookupBlob(vn->blobstore, digest, &out)) < 0) {
        return status;
    }
    out->blob->QueueUnlink();
    vn_release(out); // We looked up the blob -- release it too.
    return NO_ERROR;
}

static mx_status_t fs_rename_none(vnode_t* olddir, vnode_t* newdir, const char* oldname,
                                  size_t oldlen, const char* newname, size_t newlen,
                                  bool src_must_be_dir, bool dst_must_be_dir) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_link_none(vnode_t* vndir, const char* name, size_t len, vnode_t* target) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_sync(vnode_t* vn) {
    // TODO(smklein): For now, this is a no-op, but it will change
    // once the kBlobFlagSync flag is in use.
    return NO_ERROR;
}

vnode_ops_t blobstore_ops = {
    .release = fs_release,
    .open = fs_open,
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .lookup = fs_lookup,
    .getattr = fs_getattr,
    .setattr = fs_setattr_none,
    .readdir = fs_readdir_none,
    .create = fs_create,
    .ioctl = fs_ioctl,
    .unlink = fs_unlink,
    .truncate = fs_truncate_none,
    .rename = fs_rename_none,
    .link = fs_link_none,
    .sync = fs_sync,
};

} // namespace blobstore
