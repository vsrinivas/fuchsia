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

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <stdlib.h>
#include <string.h>

#include "vfs.h"
#include "dnode.h"

#define MXDEBUG 0

#define MAXBLOCKS 64
#define BLOCKSIZE 8192

typedef struct mnode mnode_t;
struct mnode {
    vnode_t vn;
    size_t datalen;
    uint8_t* block[MAXBLOCKS];
};

mx_status_t mem_get_node(vnode_t** out, mx_device_t* dev);

static void mem_release(vnode_t* vn) {
}

static mx_status_t mem_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    vn_acquire(vn);
    return NO_ERROR;
}

static mx_status_t mem_close(vnode_t* vn) {
    vn_release(vn);
    return NO_ERROR;
}

static ssize_t mem_read(vnode_t* vn, void* _data, size_t len, size_t off) {
    mnode_t* mem = vn->pdata;
    uint8_t* data = _data;
    ssize_t count = 0;
    if (off >= mem->datalen)
        return 0;
    if (len > (mem->datalen - off))
        len = mem->datalen - off;

    size_t bno = off / BLOCKSIZE;
    off = off % BLOCKSIZE;
    while (len > 0) {
        size_t xfer = (BLOCKSIZE - off);
        if (len < xfer)
            xfer = len;
        if (mem->block[bno] == NULL) {
            xprintf("mem_read: hole at %zu\n", bno);
            memset(data, 0, xfer);
        } else {
            memcpy(data, mem->block[bno] + off, xfer);
        }
        data += xfer;
        len -= xfer;
        count += xfer;
        bno++;
        off = 0;
    }
    return count;
}

static ssize_t mem_write(vnode_t* vn, const void* _data, size_t len, size_t off) {
    mnode_t* mem = vn->pdata;
    const uint8_t* data = _data;
    ssize_t count = 0;
    size_t bno = off / BLOCKSIZE;
    off = off % BLOCKSIZE;
    while (len > 0) {
        size_t xfer = (BLOCKSIZE - off);
        if (len < xfer)
            xfer = len;
        if (bno >= MAXBLOCKS) {
            return count ? count : ERR_NO_MEMORY;
        }
        if (mem->block[bno] == NULL) {
            xprintf("mem_write: alloc at %zu\n", bno);
            if ((mem->block[bno] = calloc(1, BLOCKSIZE)) == NULL) {
                return count ? count : ERR_NO_MEMORY;
            }
        }
        memcpy(mem->block[bno] + off, data, xfer);

        size_t pos = bno * BLOCKSIZE + off + xfer;
        if (pos > mem->datalen)
            mem->datalen = pos;

        data += xfer;
        len -= xfer;
        count += xfer;
        bno++;
        off = 0;
    }
    return count;
}

ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off) {
    return ERR_NOT_SUPPORTED;
}

ssize_t memfs_write_none(vnode_t* vn, const void* data, size_t len, size_t off) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_lookup(vnode_t* parent, vnode_t** out, const char* name, size_t len) {
    if (parent->dnode == NULL) {
        return ERR_NOT_FOUND;
    }
    dnode_t* dn;
    mx_status_t r = dn_lookup(parent->dnode, &dn, name, len);
    if (r >= 0) {
        *out = dn->vnode;
    }
    return r;
}

static mx_status_t mem_getattr(vnode_t* vn, vnattr_t* attr) {
    mnode_t* mem = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if (vn->dnode == NULL) {
        attr->size = mem->datalen;
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    return NO_ERROR;
}

mx_status_t memfs_readdir(vnode_t* parent, void* cookie, void* data, size_t len) {
    if (parent->dnode == NULL) {
        // TODO: not directory error?
        return ERR_NOT_FOUND;
    }
    return dn_readdir(parent->dnode, cookie, data, len);
}

static mx_status_t _mem_create(mnode_t* parent, mnode_t** out,
                               const char* name, size_t namelen);

static mx_status_t mem_create(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    mnode_t* parent = vn->pdata;
    mnode_t* mem;
    mx_status_t r = _mem_create(parent, &mem, name, len);
    if (r >= 0) {
        *out = &mem->vn;
    }
    return r;
}

static mx_status_t mem_gethandles(vnode_t* vn, mx_handle_t* handles, uint32_t* ids) {
    return ERR_NOT_SUPPORTED;
}

/*static*/ vnode_ops_t vn_mem_ops = {
    .release = mem_release,
    .open = mem_open,
    .close = mem_close,
    .read = mem_read,
    .write = mem_write,
    .lookup = memfs_lookup,
    .getattr = mem_getattr,
    .readdir = memfs_readdir,
    .create = mem_create,
    .gethandles = mem_gethandles,
};

static vnode_ops_t vn_mem_ops_dir = {
    .release = mem_release,
    .open = mem_open,
    .close = mem_close,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = mem_getattr,
    .readdir = memfs_readdir,
    .create = mem_create,
    .gethandles = mem_gethandles,
};

static dnode_t mem_root_dn = {
    .name = "tmp",
    .flags = 3,
    .refcount = 1,
    .children = LIST_INITIAL_VALUE(mem_root_dn.children),
};

static mnode_t mem_root = {
    .vn = {
        .ops = &vn_mem_ops_dir,
        .refcount = 1,
        .pdata = &mem_root,
        .dnode = &mem_root_dn,
        .dn_list = LIST_INITIAL_VALUE(mem_root.vn.dn_list),
    },
};

static mx_status_t _mem_create(mnode_t* parent, mnode_t** out,
                               const char* name, size_t namelen) {
    if ((parent == NULL) || (parent->vn.dnode == NULL)) {
        return ERR_INVALID_ARGS;
    }

    mnode_t* mem;
    if ((mem = calloc(1, sizeof(mnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("mem_create: vn=%p, parent=%p name='%.*s'\n",
            mem, parent, (int)namelen, name);

    mem->vn.ops = &vn_mem_ops;
    mem->vn.refcount = 1;
    mem->vn.pdata = mem;
    list_initialize(&mem->vn.dn_list);

    mx_status_t r;
    dnode_t* dn;
    if ((r = dn_lookup(parent->vn.dnode, &dn, name, namelen)) == NO_ERROR) {
        free(mem);
        return ERR_ALREADY_EXISTS;
    }

    if ((r = dn_create(&dn, name, namelen, &mem->vn)) < 0) {
        free(mem);
        return r;
    }
    dn_add_child(parent->vn.dnode, dn);

    *out = mem;
    return NO_ERROR;
}

vnode_t* memfs_get_root(void) {
    mem_root_dn.vnode = &mem_root.vn;
    return &mem_root.vn;
}


static dnode_t vfs_root_dn = {
    .name = "<root>",
    .flags = 6,
    .refcount = 1,
    .children = LIST_INITIAL_VALUE(vfs_root_dn.children),
    .parent = &vfs_root_dn,
};

static mnode_t vfs_root = {
    .vn = {
        .ops = &vn_mem_ops_dir,
        .refcount = 1,
        .pdata = &vfs_root,
        .dnode = &vfs_root_dn,
        .dn_list = LIST_INITIAL_VALUE(vfs_root.vn.dn_list),
    },
};

vnode_t* vfs_get_root(void) {
    if (vfs_root_dn.vnode == NULL) {
        vfs_root_dn.vnode = &vfs_root.vn;
        //TODO implement fs mount mechanism
        dn_add_child(&vfs_root_dn, devfs_get_root()->dnode);
        dn_add_child(&vfs_root_dn, bootfs_get_root()->dnode);
        dn_add_child(&vfs_root_dn, memfs_get_root()->dnode);
    }
    return &vfs_root.vn;
}