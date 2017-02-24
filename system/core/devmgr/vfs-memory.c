// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dnode.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MXDEBUG 0

#define MINFS_MAX_FILE_SIZE (8192 * 8192)

mx_status_t memfs_get_node(vnode_t** out, mx_device_t* dev);
mx_status_t memfs_can_unlink(dnode_t* dn);


//TODO: can we collapse vn->memfs_flags into vn->flags to avoid confusion?

static void memfs_release(vnode_t* vn) {
    xprintf("memfs: vn %p destroyed\n", vn);
    if ((vn->vmo != MX_HANDLE_INVALID) &&
        (!(vn->memfs_flags & MEMFS_FLAG_VMO_REUSE))) {
        mx_status_t r = mx_handle_close(vn->vmo);
        if (r < 0) {
            printf("unexpected error closing vfs vmo handle %d\n", r);
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

static ssize_t memfs_read(vnode_t* vn, void* data, size_t len, size_t off) {
    if ((off >= vn->length) || (vn->vmo == MX_HANDLE_INVALID)) {
        return 0;
    }

    size_t actual;
    mx_status_t status;
    if ((status = mx_vmo_read(vn->vmo, data, off, len, &actual)) != NO_ERROR) {
        return status;
    }
    return actual;
}

static ssize_t memfs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    mx_status_t status;
    size_t newlen = off + len;
    newlen = newlen > MINFS_MAX_FILE_SIZE ? MINFS_MAX_FILE_SIZE : newlen;

    // TODO(smklein): Round up to PAGE_SIZE increments to reduce overhead on a series of small
    // writes.
    if (vn->vmo == MX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = mx_vmo_create(newlen, 0, &vn->vmo)) != NO_ERROR) {
            return status;
        }
    } else if (newlen > vn->length) {
        // Accessing beyond the end of the file? Extend it.
        if ((status = mx_vmo_set_size(vn->vmo, newlen)) != NO_ERROR) {
            return status;
        }
    }

    size_t actual;
    if ((status = mx_vmo_write(vn->vmo, data, off, len, &actual)) != NO_ERROR) {
        return status;
    }

    if (newlen > vn->length) {
        vn->length = newlen;
    }
    if (actual == 0 && off >= MINFS_MAX_FILE_SIZE) {
        // short write because we're beyond the end of the permissible length
        return ERR_FILE_BIG;
    }
    vn->modify_time = mx_time_get(MX_CLOCK_UTC);
    return actual;
}

mx_status_t memfs_truncate(vnode_t* vn, size_t len) {
    mx_status_t status;
    len = len > MINFS_MAX_FILE_SIZE ? MINFS_MAX_FILE_SIZE : len;

    if (vn->vmo == MX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = mx_vmo_create(len, 0, &vn->vmo)) != NO_ERROR) {
            return status;
        }
    } else if ((len < vn->length) && (len % PAGE_SIZE != 0)) {
        // TODO(smklein): Remove this case when the VMO system causes 'shrinking to a partial page'
        // to fill the end of that page with zeroes.
        //
        // Currently, if the file is truncated to a 'partial page', an later re-expanded, then the
        // partial page is *not necessarily* filled with zeroes. As a consequence, we manually must
        // fill the portion between "len" and the next highest page (or vn->length, whichever
        // is smaller) with zeroes.
        char buf[PAGE_SIZE];
        size_t ppage_size = PAGE_SIZE - (len % PAGE_SIZE);
        ppage_size = len + ppage_size < vn->length ? ppage_size : vn->length - len;
        memset(buf, 0, ppage_size);
        size_t actual;
        status = mx_vmo_write(vn->vmo, buf, len, ppage_size, &actual);
        if ((status != NO_ERROR) || (actual != ppage_size)) {
            return status != NO_ERROR ? ERR_IO : status;
        } else if ((status = mx_vmo_set_size(vn->vmo, len)) != NO_ERROR) {
            return status;
        }
    } else if ((status = mx_vmo_set_size(vn->vmo, len)) != NO_ERROR) {
        return status;
    }

    vn->length = len;
    vn->modify_time = mx_time_get(MX_CLOCK_UTC);
    return NO_ERROR;
}

mx_status_t memfs_link(vnode_t* vndir, const char* name, size_t len, vnode_t* target) {
    if (!VNODE_IS_DIR(vndir))
        return ERR_BAD_STATE;
    if ((len == 1) && (name[0] == '.'))
        return ERR_BAD_STATE;
    if ((len == 2) && (name[0] == '.') && (name[1] == '.'))
        return ERR_BAD_STATE;

    dnode_t *targetdn;
    mx_status_t r;

    if (VNODE_IS_DIR(target)) {
        // The target must not be a directory
        return ERR_NOT_FILE;
    } else if ((r = dn_lookup(vndir->dnode, &targetdn, name, len)) == 0) {
        // The destination should not exist
        return ERR_ALREADY_EXISTS;
    }

    // Make a new dnode for the new name, attach the target vnode to it
    if ((r = dn_create(&targetdn, name, len, target)) < 0) {
        return r;
    }
    // Attach the new dnode to its parent
    dn_add_child(vndir->dnode, targetdn);

    return NO_ERROR;
}

mx_status_t memfs_rename(vnode_t* olddir, vnode_t* newdir, const char* oldname, size_t oldlen,
                         const char* newname, size_t newlen, bool src_must_be_dir,
                         bool dst_must_be_dir) {
    if (!VNODE_IS_DIR(olddir) || !VNODE_IS_DIR(newdir))
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
    if (!DNODE_IS_DIR(olddn) && (src_must_be_dir || dst_must_be_dir)) {
        return ERR_NOT_DIR;
    }

    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (DNODE_IS_DIR(olddn)) {
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
        if (DNODE_IS_DIR(olddn) != DNODE_IS_DIR(targetdn)) {
            // Cannot rename files to directories (and vice versa)
            return ERR_INVALID_ARGS;
        } else if ((r = memfs_can_unlink(targetdn)) < 0) {
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

mx_status_t memfs_rename_none(vnode_t* olddir, vnode_t* newdir, const char* oldname, size_t oldlen,
                            const char* newname, size_t newlen, bool src_must_be_dir,
                            bool dst_must_be_dir) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_link_none(vnode_t* vndir, const char* name, size_t len, vnode_t* target) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_sync(vnode_t* vn) {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    return NO_ERROR;
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
    assert(r <= 0);
    if (r == 0) {
        vn_acquire(dn->vnode);
        *out = dn->vnode;
    }
    return r;
}

mx_status_t memfs_lookup_none(vnode_t* parent, vnode_t** out, const char* name, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t memfs_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (vn->dnode == NULL) {
        attr->size = vn->length;
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    attr->create_time = vn->create_time;
    attr->modify_time = vn->modify_time;
    attr->size = vn->length;
    return NO_ERROR;
}

static mx_status_t memfs_setattr(vnode_t* vn, vnattr_t* attr) {
    if ((attr->valid & ~(ATTR_MTIME)) != 0) {
        // only attr currently supported
        return ERR_INVALID_ARGS;
    }
    if (attr->valid & ATTR_MTIME) {
        vn->modify_time = attr->modify_time;
    }
    return NO_ERROR;
}

static mx_status_t memfs_setattr_none(vnode_t* vn, vnattr_t* attr) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_readdir(vnode_t* parent, void* cookie, void* data, size_t len) {
    if (parent->dnode == NULL) {
        return ERR_NOT_DIR;
    }
    return dn_readdir(parent->dnode, cookie, data, len);
}

mx_status_t memfs_readdir_none(vnode_t* parent, void* cookie, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t _memfs_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t flags);

// postcondition: reference taken on vn returned through "out"
static mx_status_t memfs_create(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    vnode_t* vn;
    uint32_t flags = S_ISDIR(mode)
        ? MEMFS_TYPE_DIR
        : MEMFS_TYPE_DATA;
    mx_status_t r = _memfs_create(vndir, &vn, name, len, flags);
    if (r >= 0) {
        vn_acquire(vn);
        *out = vn;
    }
    return r;
}

mx_status_t memfs_create_none(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode) {
    return ERR_NOT_SUPPORTED;
}

ssize_t memfs_ioctl_none(vnode_t* vn, uint32_t op,
                         const void* in_data, size_t in_len,
                         void* out_data, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t memfs_can_unlink(dnode_t* dn) {
    bool is_directory = DNODE_IS_DIR(dn);
    if (is_directory && (dn->vnode->refcount > 1)) {
        // Cannot unlink an open directory
        return ERR_BAD_STATE;
    } else if (!list_is_empty(&dn->children)) {
        // Cannot unlink non-empty directory
        return ERR_BAD_STATE;
    } else if (dn->vnode->remote > 0) {
        // Cannot unlink mount points
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

mx_status_t memfs_unlink(vnode_t* vn, const char* name, size_t len, bool must_be_dir) {
    xprintf("memfs_unlink(%p,'%.*s')\n", vn, (int)len, name);
    if (vn->dnode == NULL) {
        return ERR_NOT_DIR;
    }
    dnode_t* dn;
    mx_status_t r;
    if ((r = dn_lookup(vn->dnode, &dn, name, len)) < 0) {
        return r;
    }
    bool is_directory = (dn->vnode->dnode != NULL);
    if (!is_directory && must_be_dir) {
        return ERR_NOT_DIR;
    }
    if ((r = memfs_can_unlink(dn)) < 0) {
        return r;
    }
    dn_delete(dn);
    return NO_ERROR;
}

mx_status_t memfs_unlink_none(vnode_t* vn, const char* name, size_t len, bool must_be_dir) {
    return ERR_NOT_SUPPORTED;
}

static void dir_release(vnode_t* vn) {
    xprintf("memfs: directory vn %p destroyed\n", vn);

    free(vn);
}

ssize_t vmo_read(vnode_t* vn, void* data, size_t len, size_t off) {
    if (off > vn->length)
        return 0;
    size_t rlen = vn->length - off;
    if (len > rlen)
        len = rlen;
    mx_status_t r = mx_vmo_read(vn->vmo, data, vn->offset+off, len, &len);
    if (r < 0) {
        return r;
    }
    return len;
}

mx_status_t vmo_getattr(vnode_t* vn, vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->size = vn->length;
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->create_time = vn->create_time;
    attr->modify_time = vn->modify_time;
    return NO_ERROR;
}

static ssize_t vmo_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    size_t rlen;
    if (off+len > vn->length) {
        // TODO(orr): grow vmo to support extending length
        return ERR_NOT_SUPPORTED;
    }
    mx_status_t r = mx_vmo_write(vn->vmo, data, vn->offset+off, len, &rlen);
    if (r < 0) {
        return r;
    }
    vn->modify_time = mx_time_get(MX_CLOCK_UTC);
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

static vnode_ops_t vn_memfs_ops = {
    .release = memfs_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read,
    .write = memfs_write,
    .lookup = memfs_lookup_none,
    .getattr = memfs_getattr,
    .setattr = memfs_setattr,
    .readdir = memfs_readdir_none,
    .create = memfs_create,
    .unlink = memfs_unlink_none,
    .truncate = memfs_truncate,
    .rename = memfs_rename_none,
    .link = memfs_link_none,
    .sync = memfs_sync,
};

static vnode_ops_t vn_vmo_ops = {
    .release = memfs_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = vmo_read,
    .write = vmo_write,
    .lookup = memfs_lookup_none,
    .getattr = vmo_getattr,
    .setattr = memfs_setattr_none,
    .readdir = memfs_readdir_none,
    .create = memfs_create_none,
    .unlink = memfs_unlink_none,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename_none,
    .link = memfs_link_none,
    .sync = memfs_sync,
};

static vnode_ops_t vn_memfs_ops_dir = {
    .release = dir_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = memfs_getattr,
    .setattr = memfs_setattr,
    .readdir = memfs_readdir,
    .create = memfs_create,
    .unlink = memfs_unlink,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename,
    .link = memfs_link,
    .sync = memfs_sync,
};

static vnode_ops_t vn_device_ops = {
    .release = device_release,
    .open = memfs_open,
    .close = memfs_close,
    .read = memfs_read_none,
    .write = memfs_write_none,
    .lookup = memfs_lookup,
    .getattr = device_getattr,
    .setattr = memfs_setattr_none,
    .readdir = memfs_readdir,
    .create = memfs_create_none,
    .ioctl = memfs_ioctl_none,
    .unlink = memfs_unlink_none,
    .truncate = memfs_truncate_none,
    .rename = memfs_rename_none,
    .link = memfs_link_none,
    .sync = memfs_sync,
};

// common memfs node creation
// postcondition: return vn linked into dir (1 ref); no extra ref returned
mx_status_t _memfs_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t flags) {
    if ((parent == NULL) || (parent->dnode == NULL)) {
        return ERR_INVALID_ARGS;
    }

    uint32_t type = flags & MEMFS_TYPE_MASK;

    vnode_t* vn;
    switch (type) {
    case MEMFS_TYPE_DATA:
        if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_memfs_ops;
        break;
    case MEMFS_TYPE_DIR:
        if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        vn->ops = &vn_memfs_ops_dir;
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
        printf("memfs_create: ERROR unknown type %d\n", type);
        return ERR_INVALID_ARGS;
    }
    xprintf("memfs_create: vn=%p, parent=%p name='%.*s'\n",
            vn, parent, (int)namelen, name);

    vn->memfs_flags = flags;
    vn->create_time = vn->modify_time = mx_time_get(MX_CLOCK_UTC);

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
    fs->ops = &vn_memfs_ops_dir;  // default: root node is a dir
    fs->refcount = 1;
    fs->dnode = dn;
    list_initialize(&fs->dn_list);
    list_initialize(&fs->watch_list);

    dn->vnode = fs;
    *out = fs;
    return NO_ERROR;
}

static vnode_t* memfs_root = NULL;
vnode_t* memfs_get_root(void) {
    if (memfs_root == NULL) {
        mx_status_t r = memfs_create_fs("tmp", &memfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'tmp' file system\n", r);
            panic();
        }
        memfs_root->refcount += 1; // one for 'created'; one for 'unlinkable'
    }
    return memfs_root;
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

static void _memfs_mount(vnode_t* parent, vnode_t* subtree) {
    if (subtree->dnode->parent) {
        // subtrees will have "parent" set, either to themselves
        // while they are standalone, or to their mount parent
        subtree->dnode->parent = NULL;
    }
    dn_add_child(parent->dnode, subtree->dnode);
}

void memfs_mount(vnode_t* parent, vnode_t* subtree) {
    mtx_lock(&vfs_lock);
    _memfs_mount(parent, subtree);
    mtx_unlock(&vfs_lock);
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

        _memfs_mount(vfs_root, devfs_get_root());
        _memfs_mount(vfs_root, bootfs_get_root());
        _memfs_mount(vfs_root, memfs_get_root());

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
    mx_status_t r = _memfs_create(parent, &vn, name, len, MEMFS_TYPE_DEVICE);
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
    r = _memfs_create(parent, &vn, pathout, strlen(pathout), MEMFS_TYPE_VMO);
    if (r < 0) {
        if (mx_handle_close(h) < 0) {
            printf("memfs_create_from_vmo: unexpected error closing handle\n");
        }
        vn_release(parent);
        return r;
    }
    vn_release(parent);

    vn->vmo = h;
    vn->offset = off;
    vn->length = len;

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
    r = _memfs_create(parent, &vn, pathout, strlen(pathout), flags); // no ref taken
    if (r != NO_ERROR) {
        vn_release(parent);
        return r;
    }

    mx_status_t unlink_r;
    bool must_be_dir = false;
    if (flags == MEMFS_TYPE_VMO) {
        // add a backing file
        mx_handle_t vmo;
        if ((r = mx_vmo_create(len, 0, &vmo)) < 0) {
            if ((unlink_r = parent->ops->unlink(parent, pathout, strlen(pathout), must_be_dir)) != NO_ERROR) {
                printf("memfs: unexpected unlink failure: %s %d\n", pathout, unlink_r);
            }
            vn_release(parent);
            return r;
        }
        vn->vmo = vmo;
        vn->offset = 0;
        vn->length = len;
    }

    r = vn->ops->write(vn, ptr, len, 0);
    if (r != (int)len) {
        if ((unlink_r = parent->ops->unlink(parent, pathout, strlen(pathout), must_be_dir)) != NO_ERROR) {
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
    r = _memfs_create(parent, &vn, pathout, strlen(pathout), MEMFS_TYPE_DIR);
    vn_release(parent);

    return r;
}
