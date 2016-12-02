// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dnode.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0


mx_handle_t vfs_get_vmofile(vnode_t* vn, mx_off_t* off, mx_off_t* len) {
    mx_handle_t vmo;
    mx_status_t status = mx_handle_duplicate(vn->vmo.h, MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER, &vmo);
    if (status < 0)
        return status;
    xprintf("vmofile: %x (%x) off=%" PRIu64 " len=%" PRIu64 "\n", vmo, vn->vmo.h, vn->vmo.offset, vn->vmo.length);

    *off = vn->vmo.offset;
    *len = vn->vmo.length;
    return vmo;
}

static mx_status_t _vnb_create(vnode_t* parent, vnode_t** out,
                               const char* name, size_t namelen,
                               mx_handle_t h, mx_off_t off, size_t datalen) {
    if (parent->dnode == NULL) {
        return ERR_NOT_DIR;
    }

    vnode_t* vnb;
    mx_status_t r = _mem_create(parent, &vnb, name, namelen, MEMFS_TYPE_VMO|MEMFS_FLAG_VMO_REUSE);
    if (r < 0) {
        if (mx_handle_close(h) < 0) {
            printf("memfs_create_from_vmo: unexpected error closing handle\n");
        }
        return r;
    }
    xprintf("vnb_create: vn=%p, parent=%p name='%.*s' datalen=%zd\n",
            vnb, parent, (int)namelen, name, datalen);

    vnb->vmo.length = datalen;
    vnb->vmo.h = h;
    vnb->vmo.offset = off;

    if (h) {
        vnb->flags |= V_FLAG_VMOFILE;
    }
    *out = vnb;

    return NO_ERROR;
}

static mx_status_t _vnb_mkdir(vnode_t* parent, vnode_t** out, const char* name, size_t namelen) {
    // TODO(orr): subsequent patch will move this to more regular vnode operations
    //printf("vnb_mkdir: parent=%p name='%.*s'\n", parent, (int)namelen, name);
    if (parent->dnode == NULL) {
        printf("bootfs: %p not a directory\n", parent);
        return ERR_NOT_DIR;
    }

    // existing directory of the same name?
    dnode_t* dn;
    if (dn_lookup(parent->dnode, &dn, name, namelen) == NO_ERROR) {
        //printf("vnb_mkdir: found vn %p\n", dn->vnode);
        if (dn->vnode->dnode != NULL) {
            // is a directory, success!
            *out = dn->vnode;
            return NO_ERROR;
        } else {
            return ERR_NOT_DIR;
        }
    }

    // create a new directory
    return _mem_create(parent, out, name, namelen, MEMFS_TYPE_DIR);
}

static mx_status_t _add_file(vnode_t* vnb, const char* path, mx_handle_t vmo,
                             mx_off_t off, size_t len) {
    mx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == NULL) {
            if (path[0] == 0) {
                return ERR_INVALID_ARGS;
            }
            return _vnb_create(vnb, &vnb, path, strlen(path), vmo, off, len);
        } else {
            if (nextpath == path)
                return ERR_INVALID_ARGS;
            r = _vnb_mkdir(vnb, &vnb, path, nextpath - path);
            if (r < 0) {
                return r;
            }
            path = nextpath + 1;
        }
    }
}

mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len) {
    return _add_file(bootfs_get_root(), path, vmo, off, len);
}

mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len) {
    return _add_file(systemfs_get_root(), path, vmo, off, len);
}

