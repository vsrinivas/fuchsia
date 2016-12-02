// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dnode.h"
#include "memfs-private.h"
#include "vfs.h"

#include <magenta/device/devmgr.h>
#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MXDEBUG 0

#define MAXBLOCKS 8192
#define BLOCKSIZE 8192

mx_status_t mem_get_node(vnode_t** out, mx_device_t* dev);
mx_status_t mem_can_unlink(dnode_t* dn);

static void mem_release(vnode_t* vn) {
    xprintf("memfs: vn %p destroyed\n", vn);

    for (int i = 0; i < MAXBLOCKS; i++) {
        if (vn->data.block[i]) {
            free(vn->data.block[i]);
        }
    }
    free(vn);
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
    uint8_t* data = _data;
    ssize_t count = 0;
    if (off >= vn->data.length)
        return 0;
    if (len > (vn->data.length - off))
        len = vn->data.length - off;

    size_t bno = off / BLOCKSIZE;
    off = off % BLOCKSIZE;
    while (len > 0) {
        size_t xfer = (BLOCKSIZE - off);
        if (len < xfer)
            xfer = len;
        if (vn->data.block[bno] == NULL) {
            xprintf("mem_read: hole at %zu\n", bno);
            memset(data, 0, xfer);
        } else {
            memcpy(data, vn->data.block[bno] + off, xfer);
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
        if (vn->data.block[bno] == NULL) {
            xprintf("mem_write: alloc at %zu\n", bno);
            if ((vn->data.block[bno] = calloc(1, BLOCKSIZE)) == NULL) {
                return count ? count : ERR_NO_MEMORY;
            }
        }
        memcpy(vn->data.block[bno] + off, data, xfer);

        size_t pos = bno * BLOCKSIZE + off + xfer;
        if (pos > vn->data.length)
            vn->data.length = pos;

        data += xfer;
        len -= xfer;
        count += xfer;
        bno++;
        off = 0;
    }
    return count;
}

mx_status_t memfs_truncate(vnode_t* vn, size_t len) {
    mx_status_t r = 0;

    while (len < vn->data.length) {
        // Truncate should make the file shorter

        // Observe the final blocks of the file first
        size_t bno = vn->data.length / BLOCKSIZE;
        size_t b_start = bno * BLOCKSIZE;

        if (b_start == vn->data.length) {
            // If the last block is empty, move to the one before
            bno--;
            b_start -= BLOCKSIZE;
        }
        // "b_start" is now guaranteed to be less than "mem->datalen"

        if (len <= b_start) {
            // Wipe out this entire block
            if (vn->data.block[bno] != NULL) {
                free(vn->data.block[bno]);
                vn->data.block[bno] = NULL;
            }
            vn->data.length = b_start;
        } else {
            // Wipe out a portion of a block
            size_t sub_block_off = len - b_start;
            memset(vn->data.block[bno] + sub_block_off, 0, BLOCKSIZE - sub_block_off);
            vn->data.length = len;
        }
    }
    if (len > vn->data.length) {
        // Truncate should make the file longer
        if (len > MAXBLOCKS * BLOCKSIZE) {
            return ERR_INVALID_ARGS;
        }
        // Memfs supports sparse files, so we can simply extend the data length,
        // and trust the rest of the filesystem to deal with this appropriately
        vn->data.length = len;
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

    dnode_t* olddn, *targetdn;
    mx_status_t r;
    // The source must exist
    if ((r = dn_lookup(olddir->dnode, &olddn, oldname, oldlen)) < 0) {
        return r;
    }
    bool srcIsFile = (olddn->vnode->dnode == NULL);
    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (!srcIsFile) {
        dnode_t* observeddn = newdir->dnode;
        // Iterate all the way up to root
        while (observeddn->parent != observeddn) {
            if (observeddn == olddn) {
                return ERR_INVALID_ARGS;
            }
            observeddn = observeddn->parent;
        }
    }

    // The destination may or may not exist
    r = dn_lookup(newdir->dnode, &targetdn, newname, newlen);
    bool target_exists = (r == NO_ERROR);
    if (target_exists) {
        // The target exists. Validate and unlink it.
        if (olddn->vnode == targetdn->vnode) {
            // Cannot rename node to itself
            return ERR_INVALID_ARGS;
        }
        bool dstIsFile = (targetdn->vnode->dnode == NULL);
        if (srcIsFile != dstIsFile) {
            // Cannot rename files to directories (and vice versa)
            return ERR_INVALID_ARGS;
        } else if ((r = mem_can_unlink(targetdn)) < 0) {
            return r;
        }
    } else if (r != ERR_NOT_FOUND) {
        return r;
    }

    // Allocate the new dnode (not yet attached to anything)
    dnode_t* newdn;
    if ((r = dn_allocate(&newdn, newname, newlen)) < 0)
        return r;

    // NOTE:
    //
    // Validation ends here, and modifications begin. Rename should not fail
    // beyond this point.

    if (target_exists)
        dn_delete(targetdn);
    // Acquire the source vnode; we're going to delete the source dnode, and we
    // need to make sure the source vnode still exists afterwards.
    vnode_t* vn = olddn->vnode;
    vn_acquire(vn); // Acquire +1
    uint32_t oldtype = DN_TYPE(olddn->flags);

    // Delete source dnode
    dn_delete(olddn); // Acquire +0

    // Bind the newdn and vn, and attach it to the destination's parent
    dn_attach(newdn, vn); // Acquire +1
    vn_release(vn); // Acquire +0. No change in refcount, no chance of deletion.
    if (vn->dnode != NULL)
        vn->dnode = newdn;
    newdn->flags |= oldtype;
    dn_add_child(newdir->dnode, newdn);

    return NO_ERROR;
}

mx_status_t mem_rename_none(vnode_t* olddir, vnode_t* newdir,
                              const char* oldname, size_t oldlen,
                              const char* newname, size_t newlen) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_sync(vnode_t* vn) {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    return NO_ERROR;
}

mx_status_t mem_truncate_none(vnode_t* vn, size_t len) {
    return ERR_NOT_SUPPORTED;
}

ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off) {
    return ERR_NOT_SUPPORTED;
}

ssize_t mem_write_none(vnode_t* vn, const void* data, size_t len, size_t off) {
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

mx_status_t mem_lookup_none(vnode_t* parent, vnode_t** out, const char* name, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t mem_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (vn->dnode == NULL) {
        attr->size = vn->data.length;
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

mx_status_t mem_readdir_none(vnode_t* parent, void* cookie, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t _mem_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t flags);

// postcondition: reference taken on vn returned through "out"
static mx_status_t mem_create(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    vnode_t* vn;
    uint32_t flags = S_ISDIR(mode)
        ? MEMFS_TYPE_DIR
        : MEMFS_TYPE_DATA;
    mx_status_t r = _mem_create(vndir, &vn, name, len, flags);
    if (r >= 0) {
        vn_acquire(vn);
        *out = vn;
    }
    return r;
}

mx_status_t mem_create_none(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    return ERR_NOT_SUPPORTED;
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

mx_status_t mem_unlink_none(vnode_t* vn, const char* name, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static void dir_release(vnode_t* vn) {
    xprintf("memfs: directory vn %p destroyed\n", vn);

    free(vn);
}

void vmo_release(vnode_t* vn) {
    xprintf("memfs: vn %p destroyed\n", vn);
    if (vn->vmo.h <= 0) {
        xprintf("vmofile_release: invalid handle\n");
    }
    if ((vn->memfs_flags & MEMFS_FLAG_VMO_REUSE) != 0) {
        // "reused" vmo's are closed by vmo owner
        mx_status_t r = mx_handle_close(vn->vmo.h);
        if (r < 0) {
            printf("unexpected error closing vfs vmo handle %d\n", r);
        }
    }
    free(vn);
}

ssize_t vmo_read(vnode_t* vn, void* data, size_t len, size_t off) {
    if (off > vn->vmo.length)
        return 0;
    size_t rlen = vn->vmo.length - off;
    if (len > rlen)
        len = rlen;
    mx_status_t r = mx_vmo_read(vn->vmo.h, data, vn->vmo.offset+off, len, &len);
    if (r < 0) {
        return r;
    }
    return len;
}

mx_status_t vmo_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->size = vn->vmo.length;
    attr->mode = V_TYPE_FILE | V_IRUSR;
    return NO_ERROR;
}

static ssize_t vmo_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    size_t rlen;
    if (off+len > vn->vmo.length) {
        // TODO(orr): grow vmo to support extending length
        return ERR_NOT_SUPPORTED;
    }
    mx_status_t r = mx_vmo_write(vn->vmo.h, data, vn->vmo.offset+off, len, &rlen);
    if (r < 0) {
        return r;
    }
    return rlen;
}

static void device_release(vnode_t* vn) {
    xprintf("devfs: vn %p destroyed\n", vn);
    if (vn->remote) {
        mx_status_t r = mx_handle_close(vn->remote);
        if (r < 0) {
            printf("device_release: unexected error closing remote %d\n", r);
        }
    }
    free(vn);
}

static mx_status_t device_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if ((vn->remote != 0) && list_is_empty(&vn->dnode->children)) {
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    attr->size = 0;
    return NO_ERROR;
}

static vnode_ops_t vn_mem_ops = {
    .release = mem_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = mem_read,
    .write = mem_write,
    .lookup = mem_lookup_none,
    .getattr = mem_getattr,
    .readdir = mem_readdir_none,
    .create = mem_create,
    .unlink = mem_unlink_none,
    .truncate = memfs_truncate,
    .rename = mem_rename_none,
    .sync = memfs_sync,
};

static vnode_ops_t vn_vmo_ops = {
    .release = vmo_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = vmo_read,
    .write = vmo_write,
    .lookup = mem_lookup_none,
    .getattr = vmo_getattr,
    .readdir = mem_readdir_none,
    .create = mem_create_none,
    .unlink = mem_unlink_none,
    .truncate = mem_truncate_none,
    .rename = mem_rename_none,
    .sync = memfs_sync,
};

static vnode_ops_t vn_mem_ops_dir = {
    .release = dir_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = mem_write_none,
    .lookup = memfs_lookup,
    .getattr = mem_getattr,
    .readdir = memfs_readdir,
    .create = mem_create,
    .unlink = memfs_unlink,
    .truncate = mem_truncate_none,
    .rename = memfs_rename,
    .sync = memfs_sync,
};

static vnode_ops_t vn_device_ops = {
    .release = device_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = mem_write_none,
    .lookup = memfs_lookup,
    .getattr = device_getattr,
    .readdir = memfs_readdir,
    .create = mem_create_none,
    .ioctl = memfs_ioctl,
    .unlink = mem_unlink_none,
    .truncate = mem_truncate_none,
    .rename = mem_rename_none,
    .sync = memfs_sync,
};

// common memfs node creation
// postcondition: return vn linked into dir (1 ref); no extra ref returned
mx_status_t _mem_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t flags) {
    if ((parent == NULL) || (parent->dnode == NULL)) {
        return ERR_INVALID_ARGS;
    }

    uint32_t type = flags & MEMFS_TYPE_MASK;

    vnode_t* vn;
    switch (type) {
    case MEMFS_TYPE_DATA:
        if ((vn = calloc(1, sizeof(vnode_t)+MAXBLOCKS*sizeof(uint8_t*))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_mem_ops;
        break;
    case MEMFS_TYPE_DIR:
        if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_mem_ops_dir;
        break;
    case MEMFS_TYPE_VMO:
        if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_vmo_ops;
        // vmo is filled in by caller
        break;
    case MEMFS_TYPE_DEVICE:
        if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_device_ops;
        break;
    default:
        printf("mem_create: ERROR unknown type %d\n", type);
        return ERR_INVALID_ARGS;
    }
    xprintf("mem_create: vn=%p, parent=%p name='%.*s'\n",
            vn, parent, (int)namelen, name);

    vn->memfs_flags = flags;

    list_initialize(&vn->dn_list);
    list_initialize(&vn->watch_list);

    dnode_t* dn;
    if (dn_lookup(parent->dnode, &dn, name, namelen) == NO_ERROR) {
        free(vn);
        return ERR_ALREADY_EXISTS;
    }

    // dnode takes a reference to the vnode
    mx_status_t r;
    if ((r = dn_create(&dn, name, namelen, vn)) < 0) {
        free(vn);
        return r;
    }

    // parent takes first reference
    dn_add_child(parent->dnode, dn);

    if (type == MEMFS_TYPE_DIR || type == MEMFS_TYPE_DEVICE) {
        vn->dnode = dn;
    }

    // returning, without incrementing refcount
    *out = vn;
    return NO_ERROR;
}

mx_status_t memfs_lookup_name(const vnode_t* vn, char* out_name, size_t out_len) {
    return dn_lookup_name(vn->dnode->parent, vn, out_name, out_len);
}

mx_status_t memfs_create_fs(const char* name, vnode_t** out) {
    uint32_t namelen = strlen(name);
    dnode_t* dn = calloc(1, sizeof(dnode_t)+namelen+1);
    if (dn == NULL) {
        return ERR_NO_MEMORY;
    }
    memcpy(dn->name, name, namelen+1);
    dn->flags = namelen;
    list_initialize(&dn->children);
    dn->parent = dn; // until we mount, we are our own parent

    vnode_t* fs = calloc(1, sizeof(vnode_t));
    if (fs == NULL) {
        return ERR_NO_MEMORY;
    }
    fs->ops = &vn_mem_ops_dir;  // default: root node is a dir
    fs->refcount = 1;
    fs->dnode = dn;
    list_initialize(&fs->dn_list);
    list_initialize(&fs->watch_list);

    dn->vnode = fs;
    *out = fs;
    return NO_ERROR;
}

static vnode_t* mem_root = NULL;
vnode_t* memfs_get_root(void) {
    if (mem_root == NULL) {
        mx_status_t r = memfs_create_fs("tmp", &mem_root);
        if (r < 0) {
            printf("fatal error %d allocating 'tmp' file system\n", r);
            panic();
        }
        mem_root->refcount += 1; // one for 'created'; one for 'unlinkable'
    }
    return mem_root;
}

static vnode_t* devfs_root = NULL;
vnode_t* devfs_get_root(void) {
    if (devfs_root == NULL) {
        mx_status_t r = memfs_create_fs("dev", &devfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'device' file system\n", r);
            panic();
        }
        devfs_root->ops = &vn_device_ops; // override
    }
    return devfs_root;
}

static vnode_t* bootfs_root = NULL;
vnode_t* bootfs_get_root(void) {
    if (bootfs_root == NULL) {
        mx_status_t r = memfs_create_fs("boot", &bootfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'boot' file system\n", r);
            panic();
        }
    }
    return bootfs_root;
}

static vnode_t* systemfs_root = NULL;
vnode_t* systemfs_get_root(void) {
    if (systemfs_root == NULL) {
        mx_status_t r = memfs_create_fs("system", &systemfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'system' file system\n", r);
            panic();
        }
    }
    return systemfs_root;
}

static void memfs_mount(vnode_t* parent, vnode_t* subtree) {
    if (subtree->dnode->parent) {
        // subtrees will have "parent" set, either to themselves
        // while they are standalone, or to their mount parent
        subtree->dnode->parent = NULL;
    }
    dn_add_child(parent->dnode, subtree->dnode);
}

// Hardcoded initialization function to create/access global root directory
static vnode_t* vfs_root = NULL;
vnode_t* vfs_create_global_root(void) {
    if (vfs_root == NULL) {
        mx_status_t r = memfs_create_fs("<root>", &vfs_root);
        if (r < 0) {
            printf("fatal error %d allocating root file system\n", r);
            panic();
        }

        memfs_mount(vfs_root, devfs_get_root());
        memfs_mount(vfs_root, bootfs_get_root());
        memfs_mount(vfs_root, memfs_get_root());
        memfs_mount(vfs_root, systemfs_get_root());

        memfs_create_directory("/data", 0);
        memfs_create_directory("/volume", 0);
        memfs_create_directory("/dev/socket", 0);
    }
    return vfs_root;
}

// precondition: no ref taken on parent
// postcondition: ref returned on out parameter
static mx_status_t _memfs_create_device_at(vnode_t* parent, vnode_t** out, const char* name, mx_handle_t h) {
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
    mx_status_t r = _mem_create(parent, &vn, name, len, MEMFS_TYPE_DEVICE);
    if (r < 0) {
        return r;
    }

    if (h) {
        // attach device
        vn->remote = h;
        vn->flags = V_FLAG_DEVICE;
    }

    vfs_notify_add(parent, name, len);
    xprintf("devfs_add_node() vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

mx_status_t memfs_create_device_at(vnode_t* parent, vnode_t** out, const char* name, mx_handle_t h) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _memfs_create_device_at(parent, out, name, h);
    mtx_unlock(&vfs_lock);
    return r;
}

static mx_status_t _memfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    if ((parent == NULL) || (target == NULL)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("memfs_add_link() p=%p name='%s'\n", parent, name ? name : "###");
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

mx_status_t memfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _memfs_add_link(parent, name, target);
    mtx_unlock(&vfs_lock);
    return r;
}

// postcondition: new vnode linked into namespace, data mapped into address space
mx_status_t memfs_create_from_vmo(const char* path, uint32_t flags,
                                  mx_handle_t vmo, mx_off_t off, mx_off_t len) {

    mx_status_t r;
    const char* pathout;
    vnode_t* parent;

    if ((r = vfs_walk(vfs_root, &parent, path, &pathout)) < 0) {
        return r;
    }

    if (strcmp(pathout, "") == 0) {
        vn_release(parent);
        return ERR_ALREADY_EXISTS;
    }

    mx_handle_t h;
    r = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &h);
    if (r < 0) {
        vn_release(parent);
        return r;
    }

    vnode_t* vn;
    r = _mem_create(parent, &vn, pathout, strlen(pathout), MEMFS_TYPE_VMO);
    if (r < 0) {
        if (mx_handle_close(h) < 0) {
            printf("memfs_create_from_vmo: unexpected error closing handle\n");
        }
        vn_release(parent);
        return r;
    }
    vn_release(parent);

    vn->vmo.h = h;
    vn->vmo.offset = off;
    vn->vmo.length = len;

    return NO_ERROR;
}

// postcondition: new vnode linked into namespace
mx_status_t memfs_create_from_buffer(const char* path, uint32_t flags,
                                     const char* ptr, mx_off_t len) {
    mx_status_t r;
    const char* pathout;
    vnode_t* parent;

    if ((r = vfs_walk(vfs_root, &parent, path, &pathout)) != NO_ERROR) {
        return r;
    }

    if (strcmp(pathout, "") == 0) {
        vn_release(parent);
        return ERR_ALREADY_EXISTS;
    }

    vnode_t* vn;
    r = _mem_create(parent, &vn, pathout, strlen(pathout), flags); // no ref taken
    if (r != NO_ERROR) {
        vn_release(parent);
        return r;
    }

    mx_status_t unlink_r;
    if (flags == MEMFS_TYPE_VMO) {
        // add a backing file
        mx_handle_t vmo;
        if ((r = mx_vmo_create(len, 0, &vmo)) < 0) {
            if ((unlink_r = parent->ops->unlink(parent, pathout, strlen(pathout))) != NO_ERROR) {
                printf("memfs: unexpected unlink failure: %s %d\n", pathout, unlink_r);
            }
            vn_release(parent);
            return r;
        }
        vn->vmo.h = vmo;
        vn->vmo.offset = 0;
        vn->vmo.length = len;
    }

    r = vn->ops->write(vn, ptr, len, 0);
    if (r != (int)len) {
        if ((unlink_r = parent->ops->unlink(parent, pathout, strlen(pathout))) != NO_ERROR) {
            printf("memfs: unexpected unlink failure: %s %d\n", pathout, unlink_r);
        }
        vn_release(parent);
        if (r < 0) {
            return r;
        }
        // wrote less than our whole buffer
        return ERR_IO;
    }
    vn_release(parent);
    return NO_ERROR;
}

// postcondition: new vnode linked into namespace
mx_status_t memfs_create_directory(const char* path, uint32_t flags) {
    mx_status_t r;
    const char* pathout;
    vnode_t* parent;

    if ((r = vfs_walk(vfs_root, &parent, path, &pathout)) < 0) {
        return r;
    }
    if (strcmp(pathout, "") == 0) {
        vn_release(parent);
        return ERR_ALREADY_EXISTS;
    }

    vnode_t* vn;
    r = _mem_create(parent, &vn, pathout, strlen(pathout), MEMFS_TYPE_DIR);
    vn_release(parent);

    return r;
}

