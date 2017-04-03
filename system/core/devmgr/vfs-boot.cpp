// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <fs/vfs.h>
#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/vfs.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {

mx_status_t VnodeVmo::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                 uint32_t* type, void* extra, uint32_t* esize) {
    mx_off_t* off = static_cast<mx_off_t*>(extra);
    mx_off_t* len = off + 1;
    mx_handle_t vmo;
    mx_status_t status = mx_handle_duplicate(vmo_, MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER, &vmo);
    if (status < 0)
        return status;
    xprintf("vmofile: %x (%x) off=%" PRIu64 " len=%" PRIu64 "\n", vmo, vmo_, offset_, length_);

    *off = offset_;
    *len = length_;
    hnds[0] = vmo;
    *type = MXIO_PROTOCOL_VMOFILE;
    *esize = sizeof(mx_off_t) * 2;
    return 1;
}

static mx_status_t vnb_create(VnodeMemfs* parent, VnodeMemfs** out,
                              const char* name, size_t namelen,
                              mx_handle_t h, mx_off_t off, size_t datalen) {
    if (parent->dnode_ == nullptr) {
        return ERR_NOT_DIR;
    }

    VnodeMemfs* vnb_fs;
    mx_status_t r = memfs_create(parent, &vnb_fs, name, namelen, MEMFS_TYPE_VMO);
    if (r < 0) {
        printf("bootfs: memfs_create('%.*s') failed: %d\n", (int)namelen, name, r);
        return r;
    }
    VnodeVmo* vnb = static_cast<VnodeVmo*>(vnb_fs);
    xprintf("vnb_create: vn=%p, parent=%p name='%.*s' datalen=%zd\n",
            vnb, parent, (int)namelen, name, datalen);
    vnb->Init(h, datalen, off);

    *out = vnb;
    return NO_ERROR;
}

static mx_status_t vnb_mkdir(VnodeMemfs* parent, VnodeMemfs** out, const char* name, size_t namelen) {
    // TODO(orr): subsequent patch will move this to more regular vnode operations
    if (!parent->IsDirectory()) {
        printf("bootfs: %p not a directory\n", parent);
        return ERR_NOT_DIR;
    }

    // existing directory of the same name?
    mxtl::RefPtr<Dnode> dn;
    if (parent->dnode_->Lookup(name, namelen, &dn) == NO_ERROR) {
        if (dn == nullptr) {
            return ERR_ALREADY_EXISTS;
        } else if (dn->IsDirectory()) {
            *out = dn->AcquireVnode();
            return NO_ERROR;
        } else {
            return ERR_NOT_DIR;
        }
    }

    // create a new directory
    return memfs_create(parent, out, name, namelen, MEMFS_TYPE_DIR);
}

static mx_status_t add_file(VnodeMemfs* vnb, const char* path, mx_handle_t vmo,
                            mx_off_t off, size_t len) {
    mx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == nullptr) {
            if (path[0] == 0) {
                return ERR_INVALID_ARGS;
            }
            return vnb_create(vnb, &vnb, path, strlen(path), vmo, off, len);
        } else {
            if (nextpath == path)
                return ERR_INVALID_ARGS;
            r = vnb_mkdir(vnb, &vnb, path, nextpath - path);
            if (r < 0) {
                return r;
            }
            path = nextpath + 1;
        }
    }
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they can
// be exposed to C:

mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len) {
    return add_file(bootfs_get_root(), path, vmo, off, len);
}

mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len) {
    return add_file(systemfs_get_root(), path, vmo, off, len);
}
