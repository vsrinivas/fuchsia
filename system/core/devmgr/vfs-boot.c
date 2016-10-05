// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include "dnode.h"

#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0

typedef struct vnboot vnboot_t;
struct vnboot {
    vnode_t vn;
    mx_handle_t vmo;
    mx_off_t off;
    void* data;
    size_t datalen;
};

mx_status_t vnb_get_node(vnode_t** out, mx_device_t* dev);

static void vnb_release(vnode_t* vn) {
    printf("bootfs: vn %p destroyed\n", vn);

    free(vn);
}

static ssize_t vnb_read(vnode_t* vn, void* data, size_t len, size_t off) {
    vnboot_t* vnb = vn->pdata;
    if (off > vnb->datalen)
        return 0;
    size_t rlen = vnb->datalen - off;
    if (len > rlen)
        len = rlen;
    memcpy(data, vnb->data + off, len);
    return len;
}

static ssize_t vnb_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t vnb_getattr(vnode_t* vn, vnattr_t* attr) {
    vnboot_t* vnb = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if (vn->dnode == NULL) {
        attr->size = vnb->datalen;
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    return NO_ERROR;
}

static mx_status_t vnb_create(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t vfs_get_vmofile(vnode_t* vn, mx_off_t* off, mx_off_t* len) {
    vnboot_t* vnb = vn->pdata;
    mx_handle_t vmo = mx_handle_duplicate(vnb->vmo, MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER);
    xprintf("vmofile: %x (%x) off=%" PRIu64 " len=%zd\n", vmo, vnb->vmo, vnb->off, vnb->datalen);
    if (vmo > 0) {
        *off = vnb->off;
        *len = vnb->datalen;
    }
    return vmo;
}

static vnode_ops_t vn_boot_ops = {
    .release = vnb_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = vnb_read,
    .write = vnb_write,
    .lookup = memfs_lookup,
    .getattr = vnb_getattr,
    .readdir = memfs_readdir,
    .create = vnb_create,
    .ioctl = memfs_ioctl,
    .unlink = memfs_unlink,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename_none,
};

static dnode_t vnb_root_dn = {
    .name = "boot",
    .flags = 4,
    .children = LIST_INITIAL_VALUE(vnb_root_dn.children),
};

static vnboot_t vnb_root = {
    .vn = {
        .ops = &vn_boot_ops,
        .refcount = 1,
        .pdata = &vnb_root,
        .dnode = &vnb_root_dn,
        .dn_list = LIST_INITIAL_VALUE(vnb_root.vn.dn_list),
        .watch_list = LIST_INITIAL_VALUE(vnb_root.vn.watch_list),
    },
};

static mx_status_t _vnb_create(vnboot_t* parent, vnboot_t** out,
                               const char* name, size_t namelen,
                               mx_handle_t vmo, mx_off_t off,
                               void* data, size_t datalen) {
    if (parent->vn.dnode == NULL) {
        return ERR_NOT_DIR;
    }

    vnboot_t* vnb;
    if ((vnb = calloc(1, sizeof(vnboot_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("vnb_create: vn=%p, parent=%p name='%.*s' datalen=%zd\n",
            vnb, parent, (int)namelen, name, datalen);

    vnb->vn.ops = &vn_boot_ops;
    vnb->vn.pdata = vnb;
    list_initialize(&vnb->vn.dn_list);
    list_initialize(&vnb->vn.watch_list);

    vnb->data = data;
    vnb->datalen = datalen;
    vnb->vmo = vmo;
    vnb->off = off;

    dnode_t* dn;
    mx_status_t r;
    // dnode takes a reference to the vnode
    if ((r = dn_create(&dn, name, namelen, &vnb->vn)) < 0) {
        free(vnb);
        return r;
    }

    if (data == NULL) {
        // no data means this is a directory,
        // so take ownership of the dnode
        vnb->vn.dnode = dn;
    }

    if (vmo) {
        vnb->vn.flags |= V_FLAG_VMOFILE;
    }

    // TODO: dups?
    dn_add_child(parent->vn.dnode, dn);
    *out = vnb;

    return NO_ERROR;
}

static mx_status_t _vnb_mkdir(vnboot_t* parent, vnboot_t** out, const char* name, size_t namelen) {
    //printf("vnb_mkdir: parent=%p name='%.*s'\n", parent, (int)namelen, name);
    if (parent->vn.dnode == NULL) {
        printf("bootfs: %p not a directory\n", parent);
        return ERR_NOT_DIR;
    }

    // existing directory of the same name?
    dnode_t* dn;
    if (dn_lookup(parent->vn.dnode, &dn, name, namelen) == NO_ERROR) {
        //printf("vnb_mkdir: found dn %p\n", dn);
        if (dn->vnode->dnode != NULL) {
            // is a directory, success!
            *out = dn->vnode->pdata;
            return NO_ERROR;
        } else {
            return ERR_NOT_DIR;
        }
    }

    // create a new directory
    return _vnb_create(parent, out, name, namelen, 0, 0, NULL, 0);
}

mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, void* data, size_t len) {
    vnboot_t* vnb = &vnb_root;
    mx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == NULL) {
            if (path[0] == 0)
                return ERR_INVALID_ARGS;
            return _vnb_create(vnb, &vnb, path, strlen(path), vmo, off, data, len);
        } else {
            if (nextpath == path)
                return ERR_INVALID_ARGS;
            r = _vnb_mkdir(vnb, &vnb, path, nextpath - path);
            if (r < 0)
                return r;
            path = nextpath + 1;
        }
    }
}

vnode_t* bootfs_get_root(void) {
    vnb_root_dn.vnode = &vnb_root.vn;
    return &vnb_root.vn;
}
