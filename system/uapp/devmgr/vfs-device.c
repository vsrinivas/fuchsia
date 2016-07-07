// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vfs.h"
#include "dnode.h"
#include "devmgr.h"

#include <system/listnode.h>

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
    printf("devfs: vn %p destroyed\n", vn);
    free(vn);
}

static mx_status_t vnd_getattr(vnode_t* vn, vnattr_t* attr) {
    mx_device_t* dev = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if ((vn->dnode == NULL) || list_is_empty(&vn->dnode->children)) {
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    mx_protocol_device_t* ops = vn->pops;
    if (ops) {
        attr->size = ops->get_size(dev, NULL);
    }
    return NO_ERROR;
}

static mx_status_t vnd_create(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    return ERR_NOT_SUPPORTED;
}

static mx_handle_t vnd_gethandles(vnode_t* vn, mx_handle_t* handles, uint32_t* ids) {
    mx_device_t* dev = vn->pdata;

    // if we are a pure directory node (no dev attached)
    // or we are a dev + directory node with children
    // fall back to the default gethandles() to get the vnode
    // handle not the device handles
    if ((dev == NULL) || (vn->dnode && (!list_is_empty(&vn->dnode->children)))) {
        return ERR_NOT_SUPPORTED;
    }

    return devmgr_get_handles(dev, handles, ids);
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
    .gethandles = vnd_gethandles,
    .ioctl = memfs_ioctl,
    .unlink = vnd_unlink,
};

static dnode_t vnd_root_dn = {
    .name = "dev",
    .flags = 3,
    .refcount = 1,
    .children = LIST_INITIAL_VALUE(vnd_root_dn.children),
};

static vnode_t vnd_root = {
    .ops = &vn_device_ops,
    .refcount = 1,
    .dnode = &vnd_root_dn,
    .dn_list = LIST_INITIAL_VALUE(vnd_root.dn_list),
};

vnode_t* devfs_get_root(void) {
    vnd_root_dn.vnode = &vnd_root;
    return &vnd_root;
}

mx_status_t devfs_add_node(vnode_t** out, vnode_t* parent, const char* name, mx_device_t* dev) {
    if ((parent == NULL) || (name == NULL)) {
        return ERR_INVALID_ARGS;
    }
    xprintf("devfs_add_node() p=%p name='%s' dev=%p\n", parent, name, dev);
    size_t len = strlen(name);

    // check for duplicate
    dnode_t* dn;
    if (dn_lookup(parent->dnode, &dn, name, len) == NO_ERROR) {
        *out = dn->vnode;
        if ((dev == NULL) && (dn->vnode->pdata == NULL)) {
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

    if (dev) {
        // attach device
        vn->pdata = dev;
        vn->pops = dev->ops;
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

    xprintf("devfs_add_node() vn=%p\n", vn);
    if (dev) {
        dev->vnode = vn;
    }
    *out = vn;
    return NO_ERROR;
}

mx_status_t devfs_add_link(vnode_t* parent, const char* name, mx_device_t* dev) {
    if ((parent == NULL) || (dev == NULL) || (dev->vnode == NULL)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("devfs_add_link() p=%p name='%s' dev=%p\n", parent, name, dev);
    mx_status_t r;
    dnode_t* dn;
    size_t len = strlen(name);
    if (dn_lookup(parent->dnode, &dn, name, len) == NO_ERROR) {
        return ERR_ALREADY_EXISTS;
    }
    if ((r = dn_create(&dn, name, len, dev->vnode)) < 0) {
        return r;
    }
    dn_add_child(parent->dnode, dn);
    return NO_ERROR;
}

mx_status_t devfs_remove(vnode_t* vn) {
    printf("devfs_remove(%p)\n", vn);
    if (vn->pdata) {
        mx_device_t* dev = vn->pdata;
        dev->vnode = NULL;
        vn->pdata = NULL;
    }

    // if this vnode is a directory, delete its dnode
    if (vn->dnode) {
        printf("devfs_remove(%p) dnode not in dn_list?\n", vn);
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

    // with all dnodes destroyed, nothing should hold a reference
    // to the vnode and it should be release()'d
    return NO_ERROR;
}
