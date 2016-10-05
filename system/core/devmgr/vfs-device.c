// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include "dnode.h"
#include "devmgr.h"

#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <stdlib.h>
#include <string.h>

#include "device-internal.h"


#define MXDEBUG 0

static void vnd_release(vnode_t* vn) {
    xprintf("devfs: vn %p destroyed\n", vn);
    free(vn);
}

static mx_status_t vnd_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if ((vn->remote != 0) && list_is_empty(&vn->dnode->children)) {
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    attr->size = 0;
    return NO_ERROR;
}

static mx_status_t vnd_create(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t vnd_unlink(vnode_t* vn, const char* name, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static vnode_ops_t vn_device_ops = {
    .release = vnd_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = vnd_getattr,
    .readdir = memfs_readdir,
    .create = vnd_create,
    .ioctl = memfs_ioctl,
    .unlink = vnd_unlink,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename_none,
};

static dnode_t vnd_root_dn = {
    .name = "dev",
    .flags = 3,
    .children = LIST_INITIAL_VALUE(vnd_root_dn.children),
};

static vnode_t vnd_root = {
    .ops = &vn_device_ops,
    .refcount = 1,
    .dnode = &vnd_root_dn,
    .dn_list = LIST_INITIAL_VALUE(vnd_root.dn_list),
    .watch_list = LIST_INITIAL_VALUE(vnd_root.watch_list),
};

vnode_t* devfs_get_root(void) {
    vnd_root_dn.vnode = &vnd_root;
    return &vnd_root;
}

static mx_status_t _devfs_add_node(vnode_t** out, vnode_t* parent, const char* name, mx_handle_t h) {
    if ((parent == NULL) || (name == NULL)) {
        return ERR_INVALID_ARGS;
    }
    xprintf("devfs_add_node() p=%p name='%s'\n", parent, name);
    size_t len = strlen(name);

    // check for duplicate
    dnode_t* dn;
    if (dn_lookup(parent->dnode, &dn, name, len) == NO_ERROR) {
        *out = dn->vnode;
        if ((h == 0) && (dn->vnode->remote == 0)) {
            // creating a duplicate directory node simply
            // returns the one that's already there
            return NO_ERROR;
        }
        return ERR_ALREADY_EXISTS;
    }

    // create vnode
    vnode_t* vn;
    if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    vn->ops = &vn_device_ops;
    list_initialize(&vn->watch_list);

    if (h) {
        // attach device
        vn->remote = h;
        vn->flags = V_FLAG_DEVICE;
    }
    list_initialize(&vn->dn_list);

    // create dnode, which takes a reference to the new vnode
    mx_status_t r;
    if ((r = dn_create(&dn, name, len, vn)) < 0) {
        free(vn);
        return r;
    }

    // add to parent dnode list
    dn_add_child(parent->dnode, dn);
    vn->dnode = dn;

    vfs_notify_add(parent, name, len);

    xprintf("devfs_add_node() vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

static mx_status_t _devfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    if ((parent == NULL) || (target == NULL)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("devfs_add_link() p=%p name='%s'\n", parent, name ? name : "###");
    mx_status_t r;
    dnode_t* dn;

    char tmp[8];
    size_t len;
    if (name == NULL) {
        //TODO: something smarter
        // right now we have so few devices and instances this is not a problem
        // but it clearly is not optimal
        // seqcount is used to avoid rapidly re-using device numbers
        for (unsigned n = 0; n < 1000; n++) {
            snprintf(tmp, sizeof(tmp), "%03u", (parent->seqcount++) % 1000);
            if (dn_lookup(parent->dnode, &dn, tmp, 3) != NO_ERROR) {
                name = tmp;
                len = 3;
                goto got_name;
            }
        }
        return ERR_ALREADY_EXISTS;
    } else {
        len = strlen(name);
        if (dn_lookup(parent->dnode, &dn, name, len) == NO_ERROR) {
            return ERR_ALREADY_EXISTS;
        }
    }
got_name:
    if ((r = dn_create(&dn, name, len, target)) < 0) {
        return r;
    }
    dn_add_child(parent->dnode, dn);
    vfs_notify_add(parent, name, len);
    return NO_ERROR;
}

mx_status_t devfs_add_node(vnode_t** out, vnode_t* parent, const char* name, mx_handle_t h) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _devfs_add_node(out, parent, name, h);
    mtx_unlock(&vfs_lock);
    return r;
}

mx_status_t devfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _devfs_add_link(parent, name, target);
    mtx_unlock(&vfs_lock);
    return r;
}

mx_status_t devfs_remove(vnode_t* vn) {
    mtx_lock(&vfs_lock);

    // hold a reference to ourselves so the rug doesn't get pulled out from under us
    vn_acquire(vn);

    xprintf("devfs_remove(%p)\n", vn);
    vn->remote = 0;

    // if this vnode is a directory, delete its dnode
    if (vn->dnode) {
        xprintf("devfs_remove(%p) delete dnode\n", vn);
        dn_delete(vn->dnode);
        vn->dnode = NULL;
    }

    // delete all dnodes that point to this vnode
    // (effectively unlink() it from every directory it is in)
    dnode_t* dn;
    while ((dn = list_peek_head_type(&vn->dn_list, dnode_t, vn_entry)) != NULL) {
        if (vn->dnode == dn) {
            vn->dnode = NULL;
        }
        dn_delete(dn);
    }

    vn_release(vn);
    mtx_unlock(&vfs_lock);

    // with all dnodes destroyed, nothing should hold a reference
    // to the vnode and it should be release()'d
    return NO_ERROR;
}
