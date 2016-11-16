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

mx_status_t vnb_get_node(vnode_t** out, mx_device_t* dev);

static void vnb_release(vnode_t* vn) {
    printf("bootfs: vn %p destroyed\n", vn);

    free(vn);
}

static ssize_t vnb_read(vnode_t* vn, void* data, size_t len, size_t off) {
    if (off > vn->vmo.length)
        return 0;
    size_t rlen = vn->vmo.length - off;
    if (len > rlen)
        len = rlen;
    memcpy(data, vn->vmo.data + off, len);
    return len;
}

static ssize_t vnb_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t vnb_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (vn->dnode == NULL) {
        attr->size = vn->vmo.length;
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
    mx_handle_t vmo;
    mx_status_t status = mx_handle_duplicate(vn->vmo.h, MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER, &vmo);
    if (status < 0)
        return status;
    xprintf("vmofile: %x (%x) off=%" PRIu64 " len=%zd\n", vmo, vn->vmo.h, vn->vmo.offset, vn->vmo.length);

    *off = vn->vmo.offset;
    *len = vn->vmo.length;
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
    .truncate = mem_truncate_none,
    .rename = mem_rename_none,
};

static dnode_t bootfs_root_dn = {
    .name = "boot",
    .flags = 4,
    .children = LIST_INITIAL_VALUE(bootfs_root_dn.children),
};

static vnode_t bootfs_root = {
    .ops = &vn_boot_ops,
    .refcount = 1,
    .dnode = &bootfs_root_dn,
    .dn_list = LIST_INITIAL_VALUE(bootfs_root.dn_list),
    .watch_list = LIST_INITIAL_VALUE(bootfs_root.watch_list),
};

static dnode_t systemfs_root_dn = {
    .name = "system",
    .flags = 6,
    .children = LIST_INITIAL_VALUE(systemfs_root_dn.children),
};

static vnode_t systemfs_root = {
    .ops = &vn_boot_ops,
    .refcount = 1,
    .dnode = &systemfs_root_dn,
    .dn_list = LIST_INITIAL_VALUE(systemfs_root.dn_list),
    .watch_list = LIST_INITIAL_VALUE(systemfs_root.watch_list),
};

static mx_status_t _vnb_create(vnode_t* parent, vnode_t** out,
                               const char* name, size_t namelen,
                               mx_handle_t vmo, mx_off_t off,
                               void* data, size_t datalen) {
    if (parent->dnode == NULL) {
        return ERR_NOT_DIR;
    }

    vnode_t* vnb;
    if ((vnb = calloc(1, sizeof(vnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("vnb_create: vn=%p, parent=%p name='%.*s' datalen=%zd\n",
            vnb, parent, (int)namelen, name, datalen);

    vnb->ops = &vn_boot_ops;
    list_initialize(&vnb->dn_list);
    list_initialize(&vnb->watch_list);

    vnb->vmo.data = data;
    vnb->vmo.length = datalen;
    vnb->vmo.h = vmo;
    vnb->vmo.offset = off;

    dnode_t* dn;
    mx_status_t r;
    // dnode takes a reference to the vnode
    if ((r = dn_create(&dn, name, namelen, vnb)) < 0) {
        free(vnb);
        return r;
    }

    if (data == NULL) {
        // no data means this is a directory,
        // so take ownership of the dnode
        vnb->dnode = dn;
    }

    if (vmo) {
        vnb->flags |= V_FLAG_VMOFILE;
    }

    // TODO: dups?
    dn_add_child(parent->dnode, dn);
    *out = vnb;

    return NO_ERROR;
}

static mx_status_t _vnb_mkdir(vnode_t* parent, vnode_t** out, const char* name, size_t namelen) {
    //printf("vnb_mkdir: parent=%p name='%.*s'\n", parent, (int)namelen, name);
    if (parent->dnode == NULL) {
        printf("bootfs: %p not a directory\n", parent);
        return ERR_NOT_DIR;
    }

    // existing directory of the same name?
    dnode_t* dn;
    if (dn_lookup(parent->dnode, &dn, name, namelen) == NO_ERROR) {
        //printf("vnb_mkdir: found dn %p\n", dn);
        if (dn->vnode->dnode != NULL) {
            // is a directory, success!
            *out = dn->vnode;
            return NO_ERROR;
        } else {
            return ERR_NOT_DIR;
        }
    }

    // create a new directory
    return _vnb_create(parent, out, name, namelen, 0, 0, NULL, 0);
}

static mx_status_t _add_file(vnode_t* vnb, const char* path, mx_handle_t vmo,
                             mx_off_t off, void* data, size_t len) {
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

mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, void* data, size_t len) {
    return _add_file(&bootfs_root, path, vmo, off, data, len);
}

mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, void* data, size_t len) {
    return _add_file(&systemfs_root, path, vmo, off, data, len);
}

vnode_t* bootfs_get_root(void) {
    bootfs_root_dn.vnode = &bootfs_root;
    return &bootfs_root;
}

vnode_t* systemfs_get_root(void) {
    systemfs_root_dn.vnode = &systemfs_root;
    return &systemfs_root;
}
