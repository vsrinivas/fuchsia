// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dnode.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/listnode.h>
#include <magenta/new.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MXDEBUG 0

namespace memfs {

constexpr size_t kMinfsMaxFileSize = (8192 * 8192);

mx_status_t memfs_get_node(VnodeMemfs** out, mx_device_t* dev);
mx_status_t memfs_can_unlink(dnode_t* dn);

static VnodeMemfs* vfs_root = nullptr;
static VnodeMemfs* memfs_root = nullptr;
static VnodeMemfs* devfs_root = nullptr;
static VnodeMemfs* bootfs_root = nullptr;
static VnodeMemfs* systemfs_root = nullptr;

VnodeMemfs::VnodeMemfs() : seqcount_(0), dnode_(nullptr), link_count_(0) {
    list_initialize(&dn_list_);
    list_initialize(&watch_list_);
    create_time_ = modify_time_ = mx_time_get(MX_CLOCK_UTC);
}
VnodeMemfs::~VnodeMemfs() {}

VnodeFile::VnodeFile() : vmo_(MX_HANDLE_INVALID), length_(0) {}
VnodeFile::~VnodeFile() {}

VnodeDir::VnodeDir() {
    link_count_ = 1; // Implied '.'
}
VnodeDir::~VnodeDir() {}

VnodeVmo::VnodeVmo() : vmo_(MX_HANDLE_INVALID), length_(0), offset_(0) {}
VnodeVmo::~VnodeVmo() {}

VnodeDevice::VnodeDevice() {
    flags_ |= V_FLAG_DEVICE;
    link_count_ = 1; // Implied '.'
}
VnodeDevice::~VnodeDevice() {}

void VnodeMemfs::Release() {
    xprintf("memfs: vn %p destroyed\n", this);
    delete this;
}

void VnodeFile::Release() {
    if (vmo_ != MX_HANDLE_INVALID) {
        mx_handle_close(vmo_);
    }
    delete this;
}

void VnodeDevice::Release() {
    xprintf("devfs: vn %p destroyed\n", this);
    if (IsRemote()) {
        mx_status_t r = mx_handle_close(DetachRemote());
        if (r < 0) {
            printf("device_release: unexected error closing remote %d\n", r);
        }
    }
    delete this;
}

mx_status_t VnodeMemfs::Open(uint32_t flags) {
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return ERR_NOT_DIR;
    }
    RefAcquire();
    return NO_ERROR;
}

mx_status_t VnodeMemfs::Close() {
    RefRelease();
    return NO_ERROR;
}

ssize_t VnodeFile::Read(void* data, size_t len, size_t off) {
    if ((off >= length_) || (vmo_ == MX_HANDLE_INVALID)) {
        return 0;
    }

    size_t actual;
    mx_status_t status;
    if ((status = mx_vmo_read(vmo_, data, off, len, &actual)) != NO_ERROR) {
        return status;
    }
    return actual;
}

ssize_t VnodeVmo::Read(void* data, size_t len, size_t off) {
    if (off > length_)
        return 0;
    size_t rlen = length_ - off;
    if (len > rlen)
        len = rlen;
    mx_status_t r = mx_vmo_read(vmo_, data, offset_ + off, len, &len);
    if (r < 0) {
        return r;
    }
    return len;
}

ssize_t VnodeFile::Write(const void* data, size_t len, size_t off) {
    mx_status_t status;
    size_t newlen = off + len;
    newlen = newlen > kMinfsMaxFileSize ? kMinfsMaxFileSize : newlen;

    // TODO(smklein): Round up to PAGE_SIZE increments to reduce overhead on a series of small
    // writes.
    if (vmo_ == MX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = mx_vmo_create(newlen, 0, &vmo_)) != NO_ERROR) {
            return status;
        }
    } else if (newlen > length_) {
        // Accessing beyond the end of the file? Extend it.
        if ((status = mx_vmo_set_size(vmo_, newlen)) != NO_ERROR) {
            return status;
        }
    }

    size_t actual;
    if ((status = mx_vmo_write(vmo_, data, off, len, &actual)) != NO_ERROR) {
        return status;
    }

    if (newlen > length_) {
        length_ = newlen;
    }
    if (actual == 0 && off >= kMinfsMaxFileSize) {
        // short write because we're beyond the end of the permissible length
        return ERR_FILE_BIG;
    }
    modify_time_ = mx_time_get(MX_CLOCK_UTC);
    return actual;
}

ssize_t VnodeVmo::Write(const void* data, size_t len, size_t off) {
    size_t rlen;
    if (off+len > length_) {
        // TODO(orr): grow vmo to support extending length
        return ERR_NOT_SUPPORTED;
    }
    mx_status_t r = mx_vmo_write(vmo_, data, offset_ + off, len, &rlen);
    if (r < 0) {
        return r;
    }
    modify_time_ = mx_time_get(MX_CLOCK_UTC);
    return rlen;
}

mx_status_t VnodeDir::Lookup(fs::Vnode** out, const char* name, size_t len) {
    if (!IsDirectory()) {
        return ERR_NOT_FOUND;
    }
    dnode_t* dn;
    mx_status_t r = dn_lookup(dnode_, &dn, name, len);
    assert(r <= 0);
    if (r == 0) {
        dn->vnode->RefAcquire();
        *out = dn->vnode;
    }
    return r;
}

mx_status_t VnodeDevice::Lookup(fs::Vnode** out, const char* name, size_t len) {
    if (!IsDirectory()) {
        return ERR_NOT_FOUND;
    }
    dnode_t* dn;
    mx_status_t r = dn_lookup(dnode_, &dn, name, len);
    assert(r <= 0);
    if (r == 0) {
        dn->vnode->RefAcquire();
        *out = dn->vnode;
    }
    return r;
}

mx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return NO_ERROR;
}

mx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->size = 0;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return NO_ERROR;
}

mx_status_t VnodeVmo::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (!IsDirectory()) {
        attr->mode = V_TYPE_FILE | V_IRUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    attr->size = length_;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return NO_ERROR;
}

mx_status_t VnodeDevice::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (IsRemote() && list_is_empty(&dnode_->children)) {
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    } else {
        attr->mode = V_TYPE_DIR | V_IRUSR;
    }
    attr->size = 0;
    attr->nlink = link_count_;
    return NO_ERROR;
}

mx_status_t VnodeMemfs::Setattr(vnattr_t* attr) {
    if ((attr->valid & ~(ATTR_MTIME)) != 0) {
        // only attr currently supported
        return ERR_INVALID_ARGS;
    }
    if (attr->valid & ATTR_MTIME) {
        modify_time_ = attr->modify_time;
    }
    return NO_ERROR;
}

mx_status_t VnodeDir::Readdir(void* cookie, void* data, size_t len) {
    return dn_readdir(dnode_, cookie, data, len);
}

mx_status_t VnodeDevice::Readdir(void* cookie, void* data, size_t len) {
    if (!IsDirectory()) {
        return ERR_NOT_DIR;
    }
    return dn_readdir(dnode_, cookie, data, len);
}

// postcondition: reference taken on vn returned through "out"
mx_status_t VnodeDir::Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) {
    VnodeMemfs* vn;
    uint32_t flags = S_ISDIR(mode)
        ? MEMFS_TYPE_DIR
        : MEMFS_TYPE_DATA;
    mx_status_t r = _memfs_create(this, &vn, name, len, flags);
    if (r >= 0) {
        vn->RefAcquire();
        *out = vn;
    }
    return r;
}

mx_status_t VnodeDir::Unlink(const char* name, size_t len, bool must_be_dir) {
    xprintf("memfs_unlink(%p,'%.*s')\n", this, (int)len, name);
    if (!IsDirectory()) {
        // Calling unlink from unlinked, empty directory
        return ERR_BAD_STATE;
    }
    dnode_t* dn;
    mx_status_t r;
    if ((r = dn_lookup(dnode_, &dn, name, len)) < 0) {
        return r;
    }
    if (!dn->vnode->IsDirectory() && must_be_dir) {
        return ERR_NOT_DIR;
    }
    if ((r = memfs_can_unlink(dn)) < 0) {
        return r;
    }
    dn_delete(dn);
    return NO_ERROR;
}

mx_status_t VnodeFile::Truncate(size_t len) {
    mx_status_t status;
    len = len > kMinfsMaxFileSize ? kMinfsMaxFileSize : len;

    if (vmo_ == MX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = mx_vmo_create(len, 0, &vmo_)) != NO_ERROR) {
            return status;
        }
    } else if ((len < length_) && (len % PAGE_SIZE != 0)) {
        // TODO(smklein): Remove this case when the VMO system causes 'shrinking to a partial page'
        // to fill the end of that page with zeroes.
        //
        // Currently, if the file is truncated to a 'partial page', an later re-expanded, then the
        // partial page is *not necessarily* filled with zeroes. As a consequence, we manually must
        // fill the portion between "len" and the next highest page (or vn->length, whichever
        // is smaller) with zeroes.
        char buf[PAGE_SIZE];
        size_t ppage_size = PAGE_SIZE - (len % PAGE_SIZE);
        ppage_size = len + ppage_size < length_ ? ppage_size : length_ - len;
        memset(buf, 0, ppage_size);
        size_t actual;
        status = mx_vmo_write(vmo_, buf, len, ppage_size, &actual);
        if ((status != NO_ERROR) || (actual != ppage_size)) {
            return status != NO_ERROR ? ERR_IO : status;
        } else if ((status = mx_vmo_set_size(vmo_, len)) != NO_ERROR) {
            return status;
        }
    } else if ((status = mx_vmo_set_size(vmo_, len)) != NO_ERROR) {
        return status;
    }

    length_ = len;
    modify_time_ = mx_time_get(MX_CLOCK_UTC);
    return NO_ERROR;
}

mx_status_t VnodeDir::Rename(fs::Vnode* _newdir, const char* oldname, size_t oldlen,
                             const char* newname, size_t newlen, bool src_must_be_dir,
                             bool dst_must_be_dir) {
    VnodeMemfs* newdir = static_cast<VnodeMemfs*>(_newdir);

    if (!IsDirectory() || !newdir->IsDirectory())
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
    if ((r = dn_lookup(dnode_, &olddn, oldname, oldlen)) < 0) {
        return r;
    }
    if (!DNODE_IS_DIR(olddn) && (src_must_be_dir || dst_must_be_dir)) {
        return ERR_NOT_DIR;
    }

    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (DNODE_IS_DIR(olddn)) {
        dnode_t* observeddn = newdir->dnode_;
        // Iterate all the way up to root
        while (observeddn->parent != observeddn) {
            if (observeddn == olddn) {
                return ERR_INVALID_ARGS;
            }
            observeddn = observeddn->parent;
        }
    }

    // The destination may or may not exist
    r = dn_lookup(newdir->dnode_, &targetdn, newname, newlen);
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
    if ((r = dn_allocate(&newdn, newname, newlen)) < 0) {
        return r;
    }

    // NOTE:
    //
    // Validation ends here, and modifications begin. Rename should not fail
    // beyond this point.

    if (target_exists) {
        dn_delete(targetdn);
    }
    // Acquire the source vnode; we're going to delete the source dnode, and we
    // need to make sure the source vnode still exists afterwards.
    VnodeMemfs* vn = olddn->vnode;
    vn->RefAcquire(); // Acquire +1
    uint32_t oldtype = DN_TYPE(olddn->flags);

    // Move the children of the source dnode to the new dnode.
    list_move(&olddn->children, &newdn->children);
    dnode_t* dn;
    list_for_every_entry(&newdn->children, dn, dnode_t, dn_entry) {
        // TODO(smklein): Because each child has an explicit pointer
        // to the parent entry, we must traverse through all children
        // to rename a parent directory.
        //
        // Possible solution:
        //  - Don't reallocate the new dnode. Decouple dnode names from the
        //  rest of their structure (possibly allow in-line storage for short
        //  names). Resuing the original dnode will not require any
        //  modifications in the childs.
        dn->parent = newdn;
    }

    // Delete source dnode
    bool moved_node_was_dir = vn->IsDirectory();
    dn_delete(olddn); // Acquire +0

    // Bind the newdn and vn, and attach it to the destination's parent
    dn_attach(newdn, vn); // Acquire +1
    vn->RefRelease(); // Acquire +0. No change in refcount, no chance of deletion.
    if (moved_node_was_dir) {
        vn->dnode_ = newdn;
    }
    newdn->flags |= oldtype;
    dn_add_child(newdir->dnode_, newdn);

    return NO_ERROR;
}

mx_status_t VnodeDir::Link(const char* name, size_t len, fs::Vnode* _target) {
    VnodeMemfs* target = static_cast<VnodeMemfs*>(_target);

    if ((len == 1) && (name[0] == '.')) {
        return ERR_BAD_STATE;
    } else if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_BAD_STATE;
    } else if (!IsDirectory()) {
        // Empty, unlinked parent
        return ERR_BAD_STATE;
    }

    dnode_t *targetdn;
    mx_status_t r;

    if (target->IsDirectory()) {
        // The target must not be a directory
        return ERR_NOT_FILE;
    } else if ((r = dn_lookup(dnode_, &targetdn, name, len)) == 0) {
        // The destination should not exist
        return ERR_ALREADY_EXISTS;
    }

    // Make a new dnode for the new name, attach the target vnode to it
    if ((r = dn_create(&targetdn, name, len, target)) < 0) {
        return r;
    }
    // Attach the new dnode to its parent
    dn_add_child(dnode_, targetdn);

    return NO_ERROR;
}

mx_status_t VnodeMemfs::Sync() {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    return NO_ERROR;
}

mx_status_t memfs_can_unlink(dnode_t* dn) {
    if (!list_is_empty(&dn->children)) {
        // Cannot unlink non-empty directory
        return ERR_BAD_STATE;
    } else if (dn->vnode->IsRemote()) {
        // Cannot unlink mount points
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

mx_status_t memfs_lookup_name(const VnodeMemfs* vn, char* out_name, size_t out_len) {
    return dn_lookup_name(vn->dnode_->parent, vn, out_name, out_len);
}

static mx_status_t memfs_create_fs(const char* name, bool device, VnodeMemfs** out) {
    uint32_t namelen = static_cast<uint32_t>(strlen(name));
    dnode_t* dn = static_cast<dnode_t*>(calloc(1, sizeof(dnode_t)+namelen+1));
    if (dn == nullptr) {
        return ERR_NO_MEMORY;
    }
    memcpy(dn->name, name, namelen+1);
    dn->flags = namelen;
    list_initialize(&dn->children);
    dn->parent = dn; // until we mount, we are our own parent

    AllocChecker ac;
    VnodeMemfs* fs;
    if (device) {
        fs = new (&ac) VnodeDevice();
    } else {
        fs = new (&ac) VnodeDir();
    }
    if (!ac.check()) {
        free(dn);
        return ERR_NO_MEMORY;
    }
    fs->dnode_ = dn;

    dn->vnode = fs;
    *out = fs;
    return NO_ERROR;
}

static void _memfs_mount(VnodeMemfs* parent, VnodeMemfs* subtree) {
    if (subtree->dnode_->parent) {
        // subtrees will have "parent" set, either to themselves
        // while they are standalone, or to their mount parent
        subtree->dnode_->parent = nullptr;
    }
    dn_add_child(parent->dnode_, subtree->dnode_);
}

// precondition: no ref taken on parent
// postcondition: ref returned on out parameter
static mx_status_t _memfs_create_device_at(VnodeMemfs* parent, VnodeMemfs** out, const char* name,
                                           mx_handle_t h) {
    if ((parent == nullptr) || (name == nullptr)) {
        return ERR_INVALID_ARGS;
    }
    xprintf("devfs_add_node() p=%p name='%s'\n", parent, name);
    size_t len = strlen(name);

    // check for duplicate
    dnode_t* dn;
    if (dn_lookup(parent->dnode_, &dn, name, len) == NO_ERROR) {
        *out = dn->vnode;
        if ((h == 0) && (!dn->vnode->IsRemote())) {
            // creating a duplicate directory node simply
            // returns the one that's already there
            return NO_ERROR;
        }
        return ERR_ALREADY_EXISTS;
    }

    // create vnode
    VnodeMemfs* vn;
    mx_status_t r = _memfs_create(parent, &vn, name, len, MEMFS_TYPE_DEVICE);
    if (r < 0) {
        return r;
    }

    if (h) {
        // attach device
        vn->AttachRemote(h);
    }

    parent->NotifyAdd(name, len);
    xprintf("devfs_add_node() vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

static mx_status_t _memfs_add_link(VnodeMemfs* parent, const char* name, VnodeMemfs* target) {
    if ((parent == nullptr) || (target == nullptr)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("memfs_add_link() p=%p name='%s'\n", parent, name ? name : "###");
    mx_status_t r;
    dnode_t* dn;

    char tmp[8];
    size_t len;
    if (name == nullptr) {
        //TODO: something smarter
        // right now we have so few devices and instances this is not a problem
        // but it clearly is not optimal
        // seqcount is used to avoid rapidly re-using device numbers
        for (unsigned n = 0; n < 1000; n++) {
            snprintf(tmp, sizeof(tmp), "%03u", (parent->seqcount_++) % 1000);
            if (dn_lookup(parent->dnode_, &dn, tmp, 3) != NO_ERROR) {
                name = tmp;
                len = 3;
                goto got_name;
            }
        }
        return ERR_ALREADY_EXISTS;
    } else {
        len = strlen(name);
        if (dn_lookup(parent->dnode_, &dn, name, len) == NO_ERROR) {
            return ERR_ALREADY_EXISTS;
        }
    }
got_name:
    if ((r = dn_create(&dn, name, len, target)) < 0) {
        return r;
    }
    dn_add_child(parent->dnode_, dn);
    parent->NotifyAdd(name, len);
    return NO_ERROR;
}

// postcondition: new vnode linked into namespace, data mapped into address space
mx_status_t memfs_create_from_vmo(const char* path, uint32_t flags,
                                  mx_handle_t vmo, mx_off_t off, mx_off_t len) {

    mx_status_t r;
    const char* pathout;
    fs::Vnode* parent;

    if ((r = fs::Vfs::Walk(vfs_root, &parent, path, &pathout)) < 0) {
        return r;
    }

    if (strcmp(pathout, "") == 0) {
        parent->RefRelease();
        return ERR_ALREADY_EXISTS;
    }

    mx_handle_t h;
    r = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &h);
    if (r < 0) {
        parent->RefRelease();
        return r;
    }

    VnodeMemfs* vn_fs;
    r = _memfs_create(static_cast<VnodeMemfs*>(parent), &vn_fs, pathout, strlen(pathout),
                      MEMFS_TYPE_VMO);
    if (r < 0) {
        if (mx_handle_close(h) < 0) {
            printf("memfs_create_from_vmo: unexpected error closing handle\n");
        }
        parent->RefRelease();
        return r;
    }
    parent->RefRelease();

    VnodeVmo* vn = static_cast<VnodeVmo*>(vn_fs);
    vn->Init(h, off, len);

    return NO_ERROR;
}

// postcondition: new vnode linked into namespace
mx_status_t memfs_create_from_buffer(const char* path, uint32_t flags,
                                     const char* ptr, mx_off_t len) {
    mx_status_t r;
    const char* pathout;
    fs::Vnode* parent_vn;
    if ((r = fs::Vfs::Walk(vfs_root, &parent_vn, path, &pathout)) != NO_ERROR) {
        return r;
    }
    memfs::VnodeMemfs* parent = static_cast<memfs::VnodeMemfs*>(parent_vn);

    if (strcmp(pathout, "") == 0) {
        parent->RefRelease();
        return ERR_ALREADY_EXISTS;
    }

    VnodeMemfs* vn;
    r = _memfs_create(parent, &vn, pathout, strlen(pathout), flags); // no ref taken
    if (r != NO_ERROR) {
        parent->RefRelease();
        return r;
    }

    mx_status_t unlink_r;
    bool must_be_dir = false;
    if (flags == MEMFS_TYPE_VMO) {
        // add a backing file
        mx_handle_t vmo;
        if ((r = mx_vmo_create(len, 0, &vmo)) < 0) {
            if ((unlink_r = parent->Unlink(pathout, strlen(pathout), must_be_dir)) != NO_ERROR) {
                printf("memfs: unexpected unlink failure: %s %d\n", pathout, unlink_r);
            }
            parent->RefRelease();
            return r;
        }
        VnodeVmo* vn_vmo = static_cast<VnodeVmo*>(vn);
        vn_vmo->Init(vmo, 0, len);
    }

    r = static_cast<mx_status_t>(vn->Write(ptr, len, 0));
    if (r != (int)len) {
        if ((unlink_r = parent->Unlink(pathout, strlen(pathout), must_be_dir)) != NO_ERROR) {
            printf("memfs: unexpected unlink failure: %s %d\n", pathout, unlink_r);
        }
        parent->RefRelease();
        if (r < 0) {
            return r;
        }
        // wrote less than our whole buffer
        return ERR_IO;
    }
    parent->RefRelease();
    return NO_ERROR;
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they can
// be exposed to C:

// postcondition: new vnode linked into namespace
mx_status_t memfs_create_directory(const char* path, uint32_t flags) {
    mx_status_t r;
    const char* pathout;
    fs::Vnode* parent_vn;
    if ((r = fs::Vfs::Walk(memfs::vfs_root, &parent_vn, path, &pathout)) < 0) {
        return r;
    }
    VnodeMemfs* parent = static_cast<VnodeMemfs*>(parent_vn);

    if (strcmp(pathout, "") == 0) {
        parent->RefRelease();
        return ERR_ALREADY_EXISTS;
    }

    VnodeMemfs* vn;
    r = _memfs_create(parent, &vn, pathout, strlen(pathout), MEMFS_TYPE_DIR);
    parent->RefRelease();

    return r;
}

VnodeMemfs* systemfs_get_root() {
    if (memfs::systemfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("system", false, &memfs::systemfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'system' file system\n", r);
            panic();
        }
    }
    return memfs::systemfs_root;
}

memfs::VnodeMemfs* memfs_get_root() {
    if (memfs::memfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("tmp", false, &memfs::memfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'tmp' file system\n", r);
            panic();
        }
        memfs::memfs_root->RefAcquire(); // one for 'created'; one for 'unlinkable'
    }
    return memfs::memfs_root;
}

memfs::VnodeMemfs* devfs_get_root() {
    if (memfs::devfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("dev", true, &memfs::devfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'device' file system\n", r);
            panic();
        }
    }
    return memfs::devfs_root;
}

memfs::VnodeMemfs* bootfs_get_root() {
    if (memfs::bootfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("boot", false, &memfs::bootfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'boot' file system\n", r);
            panic();
        }
    }
    return memfs::bootfs_root;
}

mx_status_t memfs_create_device_at(memfs::VnodeMemfs* parent, memfs::VnodeMemfs** out, const char* name,
                                   mx_handle_t h) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _memfs_create_device_at(parent, out, name, h);
    mtx_unlock(&vfs_lock);
    return r;
}

// common memfs node creation
// postcondition: return vn linked into dir (1 ref); no extra ref returned
mx_status_t _memfs_create(memfs::VnodeMemfs* parent, memfs::VnodeMemfs** out,
                          const char* name, size_t namelen,
                          uint32_t flags) {
    if ((parent == nullptr) || !parent->IsDirectory()) {
        return ERR_INVALID_ARGS;
    }

    uint32_t type = flags & MEMFS_TYPE_MASK;

    AllocChecker ac;
    memfs::VnodeMemfs* vn;
    switch (type) {
    case MEMFS_TYPE_DATA:
        vn = new (&ac) memfs::VnodeFile();
        break;
    case MEMFS_TYPE_DIR:
        vn = new (&ac) memfs::VnodeDir();
        break;
    case MEMFS_TYPE_VMO:
        // vmo is filled in by caller
        vn = new (&ac) memfs::VnodeVmo();
        break;
    case MEMFS_TYPE_DEVICE:
        vn = new (&ac) memfs::VnodeDevice();
        break;
    default:
        printf("memfs_create: ERROR unknown type %d\n", type);
        return ERR_INVALID_ARGS;
    }
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    xprintf("memfs_create: vn=%p, parent=%p name='%.*s'\n",
            vn, parent, (int)namelen, name);

    memfs::dnode_t* dn;
    if (dn_lookup(parent->dnode_, &dn, name, namelen) == NO_ERROR) {
        delete vn;
        return ERR_ALREADY_EXISTS;
    }

    // dnode takes a reference to the vnode
    mx_status_t r;
    if ((r = dn_create(&dn, name, namelen, vn)) < 0) { // vn refcount = 2
        delete vn;
        return r;
    }
    vn->RefRelease(); // vn refcount = 1

    // Identify tht the vnode is a directory (vn->dnode_ != nullptr) so that
    // dn_add_child will also increment the parent link_count (after all,
    // directories contain a ".." entry, which is a link to their parent).
    if (type == MEMFS_TYPE_DIR || type == MEMFS_TYPE_DEVICE) {
        vn->dnode_ = dn;
    }

    // parent takes first reference
    dn_add_child(parent->dnode_, dn);

    // returning, without incrementing refcount
    *out = vn;
    return NO_ERROR;
}

// Hardcoded initialization function to create/access global root directory
memfs::VnodeMemfs* vfs_create_global_root() {
    if (memfs::vfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("<root>", false, &memfs::vfs_root);
        if (r < 0) {
            printf("fatal error %d allocating root file system\n", r);
            panic();
        }

        _memfs_mount(memfs::vfs_root, devfs_get_root());
        _memfs_mount(memfs::vfs_root, bootfs_get_root());
        _memfs_mount(memfs::vfs_root, memfs_get_root());

        memfs_create_directory("/data", 0);
        memfs_create_directory("/volume", 0);
        memfs_create_directory("/dev/socket", 0);
    }
    return memfs::vfs_root;
}

void memfs_mount(memfs::VnodeMemfs* parent, memfs::VnodeMemfs* subtree) {
    mtx_lock(&vfs_lock);
    _memfs_mount(parent, subtree);
    mtx_unlock(&vfs_lock);
}

mx_status_t memfs_add_link(memfs::VnodeMemfs* parent, const char* name, memfs::VnodeMemfs* target) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _memfs_add_link(parent, name, target);
    mtx_unlock(&vfs_lock);
    return r;
}

