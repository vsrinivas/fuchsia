// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vfs.h"
#include "dnode.h"

#define MXDEBUG 0

#define MAXBLOCKS 4096
#define BLOCKSIZE 8192

typedef struct mnode mnode_t;
struct mnode {
    vnode_t vn;
    size_t datalen;
    uint8_t* block[MAXBLOCKS];
};

mx_status_t mem_get_node(vnode_t** out, mx_device_t* dev);
mx_status_t mem_can_unlink(dnode_t* dn);

static void mem_release(vnode_t* vn) {
    printf("memfs: vn %p destroyed\n", vn);

    mnode_t* mem = vn->pdata;
    for (int i = 0; i < MAXBLOCKS; i++) {
        if (mem->block[i]) {
            free(mem->block[i]);
        }
    }
    free(mem);
}

mx_status_t memfs_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    if ((flags & O_DIRECTORY) && (vn->dnode == NULL)) {
        return ERR_NOT_DIR;
    }
    vn_acquire(vn);
    return NO_ERROR;
}

mx_status_t memfs_close(vnode_t* vn) {
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

mx_status_t memfs_truncate(vnode_t* vn, size_t len) {
    mnode_t* mem = vn->pdata;
    mx_status_t r = 0;

    while (len < mem->datalen) {
        // Truncate should make the file shorter

        // Observe the final blocks of the file first
        size_t bno = mem->datalen / BLOCKSIZE;
        size_t b_start = bno * BLOCKSIZE;

        if (b_start == mem->datalen) {
            // If the last block is empty, move to the one before
            bno--;
            b_start -= BLOCKSIZE;
        }
        // "b_start" is now guaranteed to be less than "mem->datalen"

        if (len <= b_start) {
            // Wipe out this entire block
            if (mem->block[bno] != NULL) {
                free(mem->block[bno]);
                mem->block[bno] = NULL;
            }
            mem->datalen = b_start;
        } else {
            // Wipe out a portion of a block
            size_t sub_block_off = len - b_start;
            memset(mem->block[bno] + sub_block_off, 0, BLOCKSIZE - sub_block_off);
            mem->datalen = len;
        }
    }
    if (len > mem->datalen) {
        // Truncate should make the file longer
        if (len > MAXBLOCKS * BLOCKSIZE) {
            return ERR_INVALID_ARGS;
        }
        // Memfs supports sparse files, so we can simply extend the data length,
        // and trust the rest of the filesystem to deal with this appropriately
        mem->datalen = len;
    }
    return r;
}

mx_status_t memfs_rename(vnode_t* olddir, vnode_t* newdir,
                         const char* oldname, size_t oldlen,
                         const char* newname, size_t newlen) {
    if ((olddir->dnode == NULL) || (newdir->dnode == NULL))
        return ERR_BAD_STATE;
    if ((oldlen == 1) && (oldname[0] == '.'))
        return ERR_BAD_STATE;
    if ((oldlen == 2) && (oldname[0] == '.') && (oldname[1] == '.'))
        return ERR_BAD_STATE;
    if ((newlen == 1) && (newname[0] == '.'))
        return ERR_BAD_STATE;
    if ((newlen == 2) && (newname[0] == '.') && (newname[1] == '.'))
        return ERR_BAD_STATE;

    // TODO(smklein) Support cross-directory rename
    if (olddir->dnode != newdir->dnode)
        return ERR_NOT_SUPPORTED;

    dnode_t* olddn, *newdn;
    mx_status_t r;
    // The source must exist
    if ((r = dn_lookup(olddir->dnode, &olddn, oldname, oldlen)) < 0) {
        return r;
    }
    // The destination may or may not exist
    r = dn_lookup(newdir->dnode, &newdn, newname, newlen);
    if (r == NO_ERROR) {
        // The target exists. Validate and unlink it.
        if (olddn->vnode == newdn->vnode) {
            // Cannot rename node to itself
            return ERR_INVALID_ARGS;
        }
        bool srcIsFile = (olddn->vnode->dnode == NULL);
        bool dstIsFile = (newdn->vnode->dnode == NULL);
        if (srcIsFile != dstIsFile) {
            // Cannot rename files to directories (and vice versa)
            return ERR_INVALID_ARGS;
        } else if ((r = mem_can_unlink(newdn)) < 0) {
            return r;
        }
        dn_delete(newdn);
    } else if (r != ERR_NOT_FOUND) {
        return r;
    }

    // Relocate olddn to newdir
    dn_move_child(newdir->dnode, olddn, newname, newlen);
    return NO_ERROR;
}

mx_status_t memfs_rename_none(vnode_t* olddir, vnode_t* newdir,
                              const char* oldname, size_t oldlen,
                              const char* newname, size_t newlen) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_truncate_none(vnode_t* vn, size_t len) {
    return ERR_NOT_SUPPORTED;
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
        vn_acquire(dn->vnode);
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

static mx_status_t _mem_create(vnode_t* parent, mnode_t** out,
                               const char* name, size_t namelen,
                               bool isdir);

static mx_status_t mem_create(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    mnode_t* mem;
    mx_status_t r = _mem_create(vn, &mem, name, len, S_ISDIR(mode));
    if (r >= 0) {
        vn_acquire(&mem->vn);
        *out = &mem->vn;
    }
    return r;
}

ssize_t memfs_ioctl(vnode_t* vn, uint32_t op,
                    const void* in_data, size_t in_len,
                    void* out_data, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t mem_can_unlink(dnode_t* dn) {
    bool isDirectory = (dn->vnode->dnode != NULL);
    if (isDirectory && (dn->vnode->refcount > 1)) {
        // Cannot unlink an open directory
        return ERR_BAD_STATE;
    } else if (!list_is_empty(&dn->children)) {
        // Cannot unlink non-empty directory
        return ERR_BAD_STATE;
    } else if (dn->vnode->flags & V_FLAG_REMOTE) {
        // Cannot unlink mount points
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

mx_status_t memfs_unlink(vnode_t* vn, const char* name, size_t len) {
    xprintf("memfs_unlink(%p,'%.*s')\n", vn, (int)len, name);
    if (vn->dnode == NULL) {
        return ERR_NOT_DIR;
    }
    dnode_t* dn;
    mx_status_t r;
    if ((r = dn_lookup(vn->dnode, &dn, name, len)) < 0) {
        return r;
    }
    if ((r = mem_can_unlink(dn)) < 0) {
        return r;
    }
    dn_delete(dn);
    return NO_ERROR;
}

static vnode_ops_t vn_mem_ops = {
    .release = mem_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = mem_read,
    .write = mem_write,
    .lookup = memfs_lookup,
    .getattr = mem_getattr,
    .readdir = memfs_readdir,
    .create = mem_create,
    .unlink = memfs_unlink,
    .truncate = memfs_truncate,
    .rename = memfs_rename,
};

static vnode_ops_t vn_mem_ops_dir = {
    .release = mem_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = mem_getattr,
    .readdir = memfs_readdir,
    .create = mem_create,
    .unlink = memfs_unlink,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename,
};

static dnode_t mem_root_dn = {
    .name = "tmp",
    .flags = 3,
    .children = LIST_INITIAL_VALUE(mem_root_dn.children),
};

static mnode_t mem_root = {
    .vn = {
        .ops = &vn_mem_ops_dir,
        .refcount = 2, // One for 'created', one for 'unlinkable'
        .pdata = &mem_root,
        .dnode = &mem_root_dn,
        .dn_list = LIST_INITIAL_VALUE(mem_root.vn.dn_list),
        .watch_list = LIST_INITIAL_VALUE(mem_root.vn.watch_list),
    },
};

static mx_status_t _mem_create(vnode_t* parent, mnode_t** out,
                               const char* name, size_t namelen,
                               bool isdir) {
    if ((parent == NULL) || (parent->dnode == NULL)) {
        return ERR_INVALID_ARGS;
    }

    mnode_t* mem;
    if ((mem = calloc(1, sizeof(mnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("mem_create: vn=%p, parent=%p name='%.*s'\n",
            mem, parent, (int)namelen, name);

    mem->vn.ops = &vn_mem_ops;
    mem->vn.pdata = mem;
    list_initialize(&mem->vn.dn_list);
    list_initialize(&mem->vn.watch_list);

    mx_status_t r;
    dnode_t* dn;
    if ((r = dn_lookup(parent->dnode, &dn, name, namelen)) == NO_ERROR) {
        free(mem);
        return ERR_ALREADY_EXISTS;
    }

    // dnode takes a reference to the vnode
    if ((r = dn_create(&dn, name, namelen, &mem->vn)) < 0) {
        free(mem);
        return r;
    }
    dn_add_child(parent->dnode, dn);

    if (isdir) {
        mem->vn.dnode = dn;
    }

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
        .watch_list = LIST_INITIAL_VALUE(vfs_root.vn.watch_list),
    },
};

static mnode_t* vn_data;
static mnode_t* vn_socket;

// Hardcoded initialization function to access global root directory
vnode_t* vfs_create_global_root(void) {
    if (vfs_root_dn.vnode == NULL) {
        vfs_root_dn.vnode = &vfs_root.vn;
        //TODO implement fs mount mechanism
        dn_add_child(&vfs_root_dn, devfs_get_root()->dnode);
        dn_add_child(&vfs_root_dn, bootfs_get_root()->dnode);
        dn_add_child(&vfs_root_dn, memfs_get_root()->dnode);
        _mem_create(&vfs_root.vn, &vn_data, "data", 4, true);
        _mem_create(devfs_get_root(), &vn_socket, "socket", 6, true);
    }
    return &vfs_root.vn;
}

// Non-intrusive node in linked list of vnodes acting as mount points
typedef struct mount_node {
    list_node_t node;
    vnode_t* vn;
} mount_node_t;

static list_node_t remote_list = LIST_INITIAL_VALUE(remote_list);

mx_status_t vfs_install_remote(vnode_t* vn, mx_handle_t h) {
    if (vn == NULL) {
        return ERR_ACCESS_DENIED;
    }

    mtx_lock(&vfs_lock);
    // We cannot mount if anything else is already installed remotely
    if (vn->remote > 0) {
        mtx_unlock(&vfs_lock);
        return ERR_ALREADY_BOUND;
    }
    // Allocate a node to track the remote handle
    mount_node_t* mount_point;
    if ((mount_point = calloc(1, sizeof(mount_node_t))) == NULL) {
        mtx_unlock(&vfs_lock);
        return ERR_NO_MEMORY;
    }
    // Save this node in the list of mounted vnodes
    mount_point->vn = vn;
    list_add_tail(&remote_list, &mount_point->node);
    vn->remote = h;
    vn->flags |= V_FLAG_REMOTE;
    mtx_unlock(&vfs_lock);

    return NO_ERROR;
}

mx_status_t vfs_uninstall_all(void) {
    mount_node_t* mount_point;
    mount_node_t* tmp;
    mtx_lock(&vfs_lock);
    list_for_every_entry_safe (&remote_list, mount_point, tmp, mount_node_t, node) {
        // TODO Send a more explicit 'unmount' signal on all remotes
        mx_handle_close(mount_point->vn->remote);
        mount_point->vn->remote = 0;
        list_delete(&mount_point->node);
    }
    mtx_unlock(&vfs_lock);
    return NO_ERROR;
}
