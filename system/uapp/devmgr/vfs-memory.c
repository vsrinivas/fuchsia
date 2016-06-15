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

#define MAXBLOCKS 64
#define BLOCKSIZE 8192

typedef struct mnode mnode_t;
struct mnode {
    vnode_t vn;
    const char* name;
    size_t namelen;
    size_t datalen;
    struct list_node children;
    struct list_node node;
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

static mx_status_t mem_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    mnode_t* parent = vn->pdata;
    mnode_t* mem;
    xprintf("mem_lookup: vn=%p name='%.*s'\n", vn, (int)len, name);
    list_for_every_entry (&parent->children, mem, mnode_t, node) {
        xprintf("? dev=%p name='%s'\n", mem, mem->name);
        if (mem->namelen != len)
            continue;
        if (memcmp(mem->name, name, len))
            continue;
        vn_acquire(&mem->vn);
        *out = &mem->vn;
        return NO_ERROR;
    }
    return ERR_NOT_FOUND;
}

static mx_status_t mem_getattr(vnode_t* vn, vnattr_t* attr) {
    mnode_t* mem = vn->pdata;
    memset(attr, 0, sizeof(vnattr_t));
    if (list_is_empty(&mem->children)) {
        attr->size = mem->datalen;
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    return NO_ERROR;
}

static mx_status_t mem_readdir(vnode_t* vn, void* cookie, void* data, size_t len) {
    mnode_t* parent = vn->pdata;
    vdircookie_t* c = cookie;
    mnode_t* last = c->p;
    size_t pos = 0;
    char* ptr = data;
    bool search = (last != NULL);
    mx_status_t r;
    mnode_t* mem;

    list_for_every_entry (&parent->children, mem, mnode_t, node) {
        if (search) {
            if (mem == last) {
                search = false;
            }
        } else {
            uint32_t vtype = list_is_empty(&mem->children) ? V_TYPE_DIR : V_TYPE_FILE;
            r = vfs_fill_dirent((void*)(ptr + pos), len - pos,
                                mem->name, mem->namelen,
                                VTYPE_TO_DTYPE(vtype));
            if (r < 0)
                break;
            last = mem;
            pos += r;
        }
    }
    c->p = last;
    return pos;
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

static vnode_ops_t vn_mem_ops = {
    .release = mem_release,
    .open = mem_open,
    .close = mem_close,
    .read = mem_read,
    .write = mem_write,
    .lookup = mem_lookup,
    .getattr = mem_getattr,
    .readdir = mem_readdir,
    .create = mem_create,
    .gethandles = mem_gethandles,
};

static mnode_t mem_root = {
    .vn = {
        .ops = &vn_mem_ops,
        .refcount = 1,
        .pdata = &mem_root,
    },
    .name = "memory",
    .namelen = 6,
    .node = LIST_INITIAL_VALUE(mem_root.node),
    .children = LIST_INITIAL_VALUE(mem_root.children),
};

static mx_status_t _mem_create(mnode_t* parent, mnode_t** out,
                               const char* name, size_t namelen) {
    mnode_t* mem;
    if ((mem = calloc(1, sizeof(mnode_t) + namelen + 1)) == NULL)
        return ERR_NO_MEMORY;
    xprintf("mem_create: vn=%p, parent=%p name='%.*s'\n",
            mem, parent, (int)namelen, name);

    char* tmp = ((char*)mem) + sizeof(mnode_t);
    memcpy(tmp, name, namelen);
    tmp[namelen] = 0;

    mem->vn.ops = &vn_mem_ops;
    mem->vn.refcount = 1;
    mem->vn.pdata = mem;
    mem->name = tmp;
    mem->namelen = namelen;
    list_initialize(&mem->node);
    list_initialize(&mem->children);

    list_add_tail(&parent->children, &mem->node);
    *out = mem;
    return NO_ERROR;
}

static mx_status_t _mem_mkdir(mnode_t* parent, mnode_t** out, const char* name, size_t namelen) {
    mnode_t* mem;
    list_for_every_entry (&parent->children, mem, mnode_t, node) {
        if (mem->namelen != namelen)
            continue;
        if (memcmp(mem->name, name, namelen))
            continue;
        *out = mem;
        return NO_ERROR;
    }
    return _mem_create(parent, out, name, namelen);
}

vnode_t* mem_get_root(void) {
    return &mem_root.vn;
}
