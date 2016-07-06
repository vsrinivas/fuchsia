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

#include <system/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0

typedef struct vnboot vnboot_t;
struct vnboot {
    vnode_t vn;
    void* data;
    size_t datalen;
};

mx_status_t vnb_get_node(vnode_t** out, mx_device_t* dev);

static void vnb_release(vnode_t* vn) {
}

static mx_status_t vnb_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    vn_acquire(vn);
    return NO_ERROR;
}

static mx_status_t vnb_close(vnode_t* vn) {
    vn_release(vn);
    return NO_ERROR;
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

static mx_status_t vnb_gethandles(vnode_t* vn, mx_handle_t* handles, uint32_t* ids) {
    return ERR_NOT_SUPPORTED;
}

static vnode_ops_t vn_boot_ops = {
    .release = vnb_release,
    .open = vnb_open,
    .close = vnb_close,
    .read = vnb_read,
    .write = vnb_write,
    .lookup = memfs_lookup,
    .getattr = vnb_getattr,
    .readdir = memfs_readdir,
    .create = vnb_create,
    .gethandles = vnb_gethandles,
};

static dnode_t vnb_root_dn = {
    .name = "boot",
    .flags = 4,
    .refcount = 1,
    .children = LIST_INITIAL_VALUE(vnb_root_dn.children),
};

static vnboot_t vnb_root = {
    .vn = {
        .ops = &vn_boot_ops,
        .refcount = 1,
        .pdata = &vnb_root,
        .dnode = &vnb_root_dn,
        .dn_list = LIST_INITIAL_VALUE(vnb_root.vn.dn_list),
    },
};

static mx_status_t _vnb_create(vnboot_t* parent, vnboot_t** out,
                               const char* name, size_t namelen,
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
    vnb->vn.refcount = 1;
    vnb->vn.pdata = vnb;
    list_initialize(&vnb->vn.dn_list);

    vnb->data = data;
    vnb->datalen = datalen;

    dnode_t* dn;
    mx_status_t r;
    if ((r = dn_create(&dn, name, namelen, &vnb->vn)) < 0) {
        free(vnb);
        return r;
    }

    if (data == NULL) {
        // no data means this is a directory,
        // so take ownership of the dnode
        vnb->vn.dnode = dn;
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
    return _vnb_create(parent, out, name, namelen, NULL, 0);
}

mx_status_t bootfs_add_file(const char* path, void* data, size_t len) {
    vnboot_t* vnb = &vnb_root;
    mx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == NULL) {
            if (path[0] == 0)
                return ERR_INVALID_ARGS;
            return _vnb_create(vnb, &vnb, path, strlen(path), data, len);
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
