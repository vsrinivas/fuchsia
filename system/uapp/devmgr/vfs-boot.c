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

#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0

typedef struct vnboot vnboot_t;
struct vnboot {
    vnode_t vn;
    const char* name;
    void* data;
    vnode_t* mounted;
    size_t namelen;
    size_t datalen;
    struct list_node children;
    struct list_node node;
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

static mx_status_t vnb_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    vnboot_t* parent = vn->pdata;
    vnboot_t* vnb;
    xprintf("vnb_lookup: vn=%p name='%.*s'\n", vn, (int)len, name);
    list_for_every_entry (&parent->children, vnb, vnboot_t, node) {
        xprintf("? dev=%p name='%s'\n", vnb, vnb->name);
        if (vnb->namelen != len)
            continue;
        if (memcmp(vnb->name, name, len))
            continue;
        if (vnb->mounted) {
            vn_acquire(vnb->mounted);
            *out = vnb->mounted;
        } else {
            vn_acquire(&vnb->vn);
            *out = &vnb->vn;
        }
        return NO_ERROR;
    }
    return ERR_NOT_FOUND;
}

static mx_status_t vnb_getattr(vnode_t* vn, vnattr_t* attr) {
    vnboot_t* vnb = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if (list_is_empty(&vnb->children)) {
        attr->size = vnb->datalen;
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    return NO_ERROR;
}

static mx_status_t vnb_readdir(vnode_t* vn, void* cookie, void* data, size_t len) {
    vnboot_t* parent = vn->pdata;
    vdircookie_t* c = cookie;
    vnboot_t* last = c->p;
    size_t pos = 0;
    char* ptr = data;
    bool search = (last != NULL);
    mx_status_t r;
    vnboot_t* vnb;

    list_for_every_entry (&parent->children, vnb, vnboot_t, node) {
        if (search) {
            if (vnb == last) {
                search = false;
            }
        } else {
            uint32_t vtype = list_is_empty(&vnb->children) ? V_TYPE_DIR : V_TYPE_FILE;
            r = vfs_fill_dirent((void*)(ptr + pos), len - pos,
                                vnb->name, vnb->namelen,
                                VTYPE_TO_DTYPE(vtype));
            if (r < 0)
                break;
            last = vnb;
            pos += r;
        }
    }
    c->p = last;
    return pos;
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
    .lookup = vnb_lookup,
    .getattr = vnb_getattr,
    .readdir = vnb_readdir,
    .create = vnb_create,
    .gethandles = vnb_gethandles,
};

static vnboot_t vnb_root = {
    .vn = {
        .ops = &vn_boot_ops,
        .refcount = 1,
        .pdata = &vnb_root,
    },
    .name = "bootfs",
    .namelen = 6,
    .node = LIST_INITIAL_VALUE(vnb_root.node),
    .children = LIST_INITIAL_VALUE(vnb_root.children),
};

static mx_status_t _vnb_create(vnboot_t* parent, vnboot_t** out,
                               const char* name, size_t namelen,
                               void* data, size_t datalen) {
    vnboot_t* vnb;
    if ((vnb = calloc(1, sizeof(vnboot_t) + namelen + 1)) == NULL)
        return ERR_NO_MEMORY;
    xprintf("vnb_create: vn=%p, parent=%p name='%.*s' datalen=%zd\n",
            vnb, parent, (int)namelen, name, datalen);

    char* tmp = ((char*)vnb) + sizeof(vnboot_t);
    memcpy(tmp, name, namelen);
    tmp[namelen] = 0;

    vnb->vn.ops = &vn_boot_ops;
    vnb->vn.refcount = 1;
    vnb->vn.pdata = vnb;
    vnb->name = tmp;
    vnb->namelen = namelen;
    vnb->data = data;
    vnb->datalen = datalen;
    list_initialize(&vnb->node);
    list_initialize(&vnb->children);

    list_add_tail(&parent->children, &vnb->node);
    *out = vnb;
    return NO_ERROR;
}

static mx_status_t _vnb_mkdir(vnboot_t* parent, vnboot_t** out, const char* name, size_t namelen) {
    vnboot_t* vnb;
    list_for_every_entry (&parent->children, vnb, vnboot_t, node) {
        if (vnb->namelen != namelen)
            continue;
        if (memcmp(vnb->name, name, namelen))
            continue;
        if (vnb->mounted)
            return ERR_NOT_SUPPORTED;
        *out = vnb;
        return NO_ERROR;
    }
    return _vnb_create(parent, out, name, namelen, NULL, 0);
}

mx_status_t vnb_add_file(const char* path, void* data, size_t len) {
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

vnode_t* vnb_get_root(void) {
    return &vnb_root.vn;
}

mx_status_t vnb_mount_at(vnode_t* vn, const char* dirname) {
    if (dirname == NULL) {
        return ERR_INVALID_ARGS;
    }
    if (strchr(dirname, '/')) {
        return ERR_INVALID_ARGS;
    }
    vnboot_t* parent;
    mx_status_t r;
    if ((r = _vnb_mkdir(&vnb_root, &parent, dirname, strlen(dirname))) < 0) {
        return r;
    }
    parent->mounted = vn;
    return NO_ERROR;
}
