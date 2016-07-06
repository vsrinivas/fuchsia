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

#include <mxu/list.h>

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
}

//XXX refcounts
static mx_status_t vnd_open_none(vnode_t** _vn, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t vnd_close_none(vnode_t* vn) {
    return NO_ERROR;
}

static mx_status_t vnd_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    mx_device_t* dev = vn->pdata;
    xprintf("vnd_open: vn=%p, dev=%p, flags=%d\n", vn, dev, flags);
    mx_status_t r = device_open(dev, flags);
    if (r == 0) {
        vn_acquire(vn);
    }
    return r;
}

static mx_status_t vnd_close(vnode_t* vn) {
// TODO: must integrate CLONE ops into refcounting first
#if 0
    mx_device_t* dev = vn->pdata;
    device_close(dev);
    vn_release(vn);
#endif
    return NO_ERROR;
}

static ssize_t vnd_read_char(vnode_t* vn, void* data, size_t len, size_t off) {
    mx_protocol_device_t* ops = vn->pops;
    return ops->read(vn->pdata, data, len, off, NULL);
}

static ssize_t vnd_write_char(vnode_t* vn, const void* data, size_t len, size_t off) {
    mx_protocol_device_t* ops = vn->pops;
    return ops->write(vn->pdata, data, len, off, NULL);
}

static ssize_t vnd_ioctl(
    vnode_t* vn, uint32_t op,
    const void* in_data, size_t in_len,
    void* out_data, size_t out_len) {

    mx_protocol_device_t* ops = vn->pops;
    return ops->ioctl(vn->pdata, op, in_data, in_len, out_data, out_len, NULL);
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

mx_status_t __mx_rio_clone(mx_handle_t h, mx_handle_t* handles, uint32_t* types);

static mx_handle_t vnd_gethandles(vnode_t* vn, mx_handle_t* handles, uint32_t* ids) {
    mx_device_t* dev = vn->pdata;

    if (dev->flags & DEV_FLAG_REMOTE) {
        mx_status_t r = __mx_rio_clone(dev->remote, handles, ids);
        return r;
    }

    if ((handles[0] = vfs_create_handle(vn)) < 0) {
        return handles[0];
    }
    ids[0] = MX_HND_TYPE_MXIO_REMOTE;

    if (dev->event <= 0) {
        return 1;
    }

    if ((handles[1] = _magenta_handle_duplicate(dev->event, MX_RIGHT_SAME_RIGHTS)) < 0) {
        _magenta_handle_close(handles[0]);
        return handles[1];
    }
    ids[1] = MX_HND_TYPE_MXIO_REMOTE;
    return 2;
}

static mx_handle_t vnd_gethandles_none(vnode_t* vn, mx_handle_t* handles, uint32_t* ids) {
    return ERR_NOT_SUPPORTED;
}

// default device ops
static vnode_ops_t vn_device_ops = {
    .release = vnd_release,
    .open = vnd_open,
    .close = vnd_close,
    .read = vnd_read_char,
    .write = vnd_write_char,
    .lookup = memfs_lookup,
    .getattr = vnd_getattr,
    .readdir = memfs_readdir,
    .create = vnd_create,
    .gethandles = vnd_gethandles,
    .ioctl = vnd_ioctl,
};

// ops for directory nodes
static vnode_ops_t vn_device_ops_none = {
    .release = vnd_release,
    .open = vnd_open_none,
    .close = vnd_close_none,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = vnd_getattr,
    .readdir = memfs_readdir,
    .create = vnd_create,
    .gethandles = vnd_gethandles_none,
    .ioctl = vnd_ioctl,
};

static dnode_t vnd_root_dn = {
    .name = "dev",
    .flags = 3,
    .refcount = 1,
    .children = LIST_INITIAL_VALUE(vnd_root_dn.children),
};

static vnode_t vnd_root = {
    .ops = &vn_device_ops_none,
    .refcount = 1,
    .pdata = &vnd_root,
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
    vn->refcount = 1;

    if (dev) {
        // attach device
        vn->pdata = dev;
        vn->pops = dev->ops;
        vn->flags = V_FLAG_DEVICE;
        vn->ops = &vn_device_ops;
    } else {
        // directory-only devfs node
        vn->ops = &vn_device_ops_none;
    }
    list_initialize(&vn->dn_list);

    // create dnode
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
    dnode_t* dn;
    while ((dn = list_peek_head_type(&vn->dn_list, dnode_t, vn_entry)) != NULL) {
        if (vn->dnode == dn) {
            vn->dnode = NULL;
        }
        dn_delete(dn);
    }
    if (vn->dnode) {
        printf("devfs_remove(%p) dnode not in dn_list?\n", vn);
        dn_delete(vn->dnode);
        vn->dnode = NULL;
    }
    vn_release(vn);
    return NO_ERROR;
}
