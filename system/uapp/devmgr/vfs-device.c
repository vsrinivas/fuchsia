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

#include <mxu/list.h>

#include <ddk/device.h>
#include <ddk/protocol/char.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <stdlib.h>
#include <string.h>

#include "device-internal.h"
#include "vfs.h"

// NOTE
// - devmgr creates protocol family devices under /dev/protocol/...
// - these devices store a pointer to a list in their protocol_ops field
//   (which is otherwise unused)
// - they also have DEV_TYPE_PROTOCOL set in their flags field
// - vnd_lookup() and vnd_readdir() know about this so they can look at
//   this list when walking through protocol devices
// - TODO: replace with symlinks once we have 'em

#define MXDEBUG 0

mx_status_t vnd_get_node(vnode_t** out, mx_device_t* dev);

static void vnd_release(vnode_t* vn) {
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

static ssize_t vnd_read(vnode_t* vn, void* data, size_t len, size_t off) {
    mx_protocol_char_t* ops = vn->pops;
    if (ops) {
        return ops->read(vn->pdata, data, len);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

static ssize_t vnd_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    mx_protocol_char_t* ops = vn->pops;
    if (ops) {
        return ops->write(vn->pdata, data, len);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

static ssize_t vnd_ioctl(
    vnode_t* vn, uint32_t op,
    const void* in_data, size_t in_len,
    void* out_data, size_t out_len) {

    mx_protocol_char_t* ops = vn->pops;
    if (ops && ops->ioctl) {
        return ops->ioctl(vn->pdata, op, in_data, in_len, out_data, out_len);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t vnd_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    mx_device_t* parent = vn->pdata;
    mx_device_t* dev;

    xprintf("vnd_lookup: vn=%p dev=%p name='%.*s'\n", vn, parent, (int)len, name);

    if (parent->flags & DEV_FLAG_PROTOCOL) {
        struct list_node* list = parent->protocol_ops;
        list_for_every_entry (list, dev, mx_device_t, pnode) {
            xprintf("? dev=%p name='%s'\n", dev, dev->name);
            size_t n = strlen(dev->name);
            if (n != len)
                continue;
            if (memcmp(dev->name, name, len))
                continue;
            xprintf("vnd_lookup: dev=%p\n", dev);
            return vnd_get_node(out, dev);
        }
        return ERR_NOT_FOUND;
    }

    list_for_every_entry (&parent->device_list, dev, mx_device_t, node) {
        xprintf("? dev=%p name='%s'\n", dev, dev->name);
        size_t n = strlen(dev->name);
        if (n != len)
            continue;
        if (memcmp(dev->name, name, len))
            continue;
        xprintf("vnd_lookup: dev=%p\n", dev);
        return vnd_get_node(out, dev);
    }
    return ERR_NOT_FOUND;
}

static mx_status_t vnd_getattr(vnode_t* vn, vnattr_t* attr) {
    mx_device_t* dev = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if (list_is_empty(&dev->device_list)) {
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    return NO_ERROR;
}

static mx_status_t vnd_readdir(vnode_t* vn, void* cookie, void* data, size_t len) {
    mx_device_t* parent = vn->pdata;
    vdircookie_t* c = cookie;
    mx_device_t* last = c->p;
    size_t pos = 0;
    char* ptr = data;
    bool search = (last != NULL);
    mx_status_t r;
    mx_device_t* dev;

    if (parent->flags & DEV_FLAG_PROTOCOL) {
        struct list_node* list = parent->protocol_ops;
        list_for_every_entry (list, dev, mx_device_t, pnode) {
            if (search) {
                if (dev == last) {
                    search = false;
                }
            } else {
                uint32_t vtype = list_is_empty(&dev->device_list) ? V_TYPE_DIR : V_TYPE_FILE;
                r = vfs_fill_dirent((void*)(ptr + pos), len - pos,
                                    dev->name, strlen(dev->name),
                                    VTYPE_TO_DTYPE(vtype));
                if (r < 0)
                    break;
                last = dev;
                pos += r;
            }
        }
        c->p = last;
        return pos;
    }

    list_for_every_entry (&parent->device_list, dev, mx_device_t, node) {
        if (search) {
            if (dev == last) {
                search = false;
            }
        } else {
            uint32_t vtype = list_is_empty(&dev->device_list) ? V_TYPE_DIR : V_TYPE_FILE;
            r = vfs_fill_dirent((void*)(ptr + pos), len - pos,
                                dev->name, strlen(dev->name),
                                VTYPE_TO_DTYPE(vtype));
            if (r < 0)
                break;
            last = dev;
            pos += r;
        }
    }
    c->p = last;
    return pos;
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

    if ((handles[1] = _magenta_handle_duplicate(dev->event)) < 0) {
        _magenta_handle_close(handles[0]);
        return handles[1];
    }
    ids[1] = MX_HND_TYPE_MXIO_REMOTE;
    return 2;
}

static vnode_ops_t vn_device_ops = {
    .release = vnd_release,
    .open = vnd_open,
    .close = vnd_close,
    .read = vnd_read,
    .write = vnd_write,
    .lookup = vnd_lookup,
    .getattr = vnd_getattr,
    .readdir = vnd_readdir,
    .create = vnd_create,
    .gethandles = vnd_gethandles,
    .ioctl = vnd_ioctl,
};

mx_status_t vnd_get_node(vnode_t** out, mx_device_t* dev) {
    if (dev->vnode) {
        vn_acquire(dev->vnode);
        *out = dev->vnode;
        return NO_ERROR;
    } else {
        vnode_t* vn;
        if ((vn = malloc(sizeof(vnode_t))) == NULL)
            return ERR_NO_MEMORY;
        vn->ops = &vn_device_ops,
        vn->vfs = NULL;
        // TODO: set this based on device properties
        vn->flags = V_FLAG_DEVICE;
        vn->refcount = 1;
        vn->pdata = dev;
        vn->pops = NULL;
        device_get_protocol(dev, MX_PROTOCOL_CHAR, (void**)&vn->pops);
        dev->vnode = vn;
        *out = vn;
        return NO_ERROR;
    }
}
