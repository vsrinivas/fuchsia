// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ddk/device.h>
#include <fs/vfs.h>
#include <magenta/device/devmgr.h>
#include <magenta/new.h>
#include <magenta/thread_annotations.h>
#include <mxio/debug.h>
#include <mxio/vfs.h>
#include <mxtl/auto_lock.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "devmgr.h"
#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {

constexpr size_t kMinfsMaxFileSize = (8192 * 8192);

mx_status_t memfs_get_node(VnodeMemfs** out, mx_device_t* dev);

static VnodeDir* vfs_root = nullptr;
static VnodeDir* memfs_root = nullptr;
static VnodeDir* devfs_root = nullptr;
static VnodeDir* bootfs_root = nullptr;
static VnodeDir* systemfs_root = nullptr;

VnodeMemfs::VnodeMemfs() : seqcount_(0), dnode_(nullptr), link_count_(0) {
    create_time_ = modify_time_ = mx_time_get(MX_CLOCK_UTC);
}
VnodeMemfs::~VnodeMemfs() {}

VnodeFile::VnodeFile() : vmo_(MX_HANDLE_INVALID), length_(0) {}
VnodeFile::~VnodeFile() {}

VnodeDir::VnodeDir() {
    link_count_ = 1; // Implied '.'
}
VnodeDir::~VnodeDir() {}

VnodeVmo::VnodeVmo(mx_handle_t vmo, mx_off_t offset, mx_off_t length) :
    vmo_(vmo), offset_(offset), length_(length) {}
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

bool VnodeDir::IsRemote() const { return remoter_.IsRemote(); }
mx_handle_t VnodeDir::DetachRemote() { return remoter_.DetachRemote(flags_); }
mx_handle_t VnodeDir::WaitForRemote() { return remoter_.WaitForRemote(flags_); }
mx_handle_t VnodeDir::GetRemote() const { return remoter_.GetRemote(); }
void VnodeDir::SetRemote(mx_handle_t remote) { return remoter_.SetRemote(remote); }

mx_status_t VnodeDir::Lookup(fs::Vnode** out, const char* name, size_t len) {
    if (!IsDirectory()) {
        return ERR_NOT_FOUND;
    }
    mxtl::RefPtr<Dnode> dn;
    mx_status_t r = dnode_->Lookup(name, len, &dn);
    MX_DEBUG_ASSERT(r <= 0);
    if (r == NO_ERROR) {
        if (dn == nullptr) {
            // Looking up our own vnode
            RefAcquire();
            *out = this;
        } else {
            // Looking up a different vnode
            *out = dn->AcquireVnode();
        }
    }
    return r;
}

mx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR | V_IWUSR;
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
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return NO_ERROR;
}

mx_status_t VnodeDevice::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    if (IsRemote() && !IsDirectory()) {
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
    if (!IsDirectory()) {
        // This WAS a directory, but it has been deleted.
        return Dnode::ReaddirStart(cookie, data, len);
    }
    return dnode_->Readdir(cookie, data, len);
}

// postcondition: reference taken on vn returned through "out"
mx_status_t VnodeDir::Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) {
    mx_status_t status;
    if ((status = CanCreate(name, len)) != NO_ERROR) {
        return status;
    }

    AllocChecker ac;
    memfs::VnodeMemfs* vn;
    if (S_ISDIR(mode)) {
        vn = new (&ac) memfs::VnodeDir();
    } else {
        vn = new (&ac) memfs::VnodeFile();
    }

    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    if ((status = AttachVnode(vn, name, len, S_ISDIR(mode))) != NO_ERROR) {
        delete vn;
        return status;
    }
    vn->RefAcquire();
    *out = vn;
    return status;
}

mx_status_t VnodeDir::Unlink(const char* name, size_t len, bool must_be_dir) {
    xprintf("memfs_unlink(%p,'%.*s')\n", this, (int)len, name);
    if (!IsDirectory()) {
        // Calling unlink from unlinked, empty directory
        return ERR_BAD_STATE;
    }
    mxtl::RefPtr<Dnode> dn;
    mx_status_t r;
    if ((r = dnode_->Lookup(name, len, &dn)) != NO_ERROR) {
        return r;
    } else if (dn == nullptr) {
        // Cannot unlink directory 'foo' using the argument 'foo/.'
        return ERR_INVALID_ARGS;
    } else if (!dn->IsDirectory() && must_be_dir) {
        // Path ending in "/" was requested, implying that the dnode must be a directory
        return ERR_NOT_DIR;
    } else if ((r = dn->CanUnlink()) != NO_ERROR) {
        return r;
    }

    dn->Detach();
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

    mxtl::RefPtr<Dnode> olddn;
    mx_status_t r;
    // The source must exist
    if ((r = dnode_->Lookup(oldname, oldlen, &olddn)) != NO_ERROR) {
        return r;
    }
    MX_DEBUG_ASSERT(olddn != nullptr);

    if (!olddn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return ERR_NOT_DIR;
    }

    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (olddn->IsSubdirectory(newdir->dnode_)) {
        return ERR_INVALID_ARGS;
    }

    // The destination may or may not exist
    mxtl::RefPtr<Dnode> targetdn;
    r = newdir->dnode_->Lookup(newname, newlen, &targetdn);
    bool target_exists = (r == NO_ERROR);
    if (target_exists) {
        MX_DEBUG_ASSERT(targetdn != nullptr);
        // The target exists. Validate and unlink it.
        if (olddn == targetdn) {
            // Cannot rename node to itself
            return ERR_INVALID_ARGS;
        }
        if (olddn->IsDirectory() != targetdn->IsDirectory()) {
            // Cannot rename files to directories (and vice versa)
            return ERR_INVALID_ARGS;
        } else if ((r = targetdn->CanUnlink()) != NO_ERROR) {
            return r;
        }
    } else if (r != ERR_NOT_FOUND) {
        return r;
    }

    // Allocate the new name for the dnode, either by
    // (1) Stealing it from the previous dnode, if it used the same name, or
    // (2) Allocating a new name, if creating a new name.
    mxtl::unique_ptr<char[]> namebuffer(nullptr);
    if (target_exists) {
        targetdn->Detach();
        namebuffer = mxtl::move(targetdn->TakeName());
    } else {
        AllocChecker ac;
        namebuffer.reset(new (&ac) char[newlen + 1]);
        if (!ac.check()) {
            return ERR_NO_MEMORY;
        }
        memcpy(namebuffer.get(), newname, newlen);
        namebuffer[newlen] = '\0';
    }

    // NOTE:
    //
    // Validation ends here, and modifications begin. Rename should not fail
    // beyond this point.

    olddn->RemoveFromParent();
    olddn->PutName(mxtl::move(namebuffer), newlen);
    Dnode::AddChild(newdir->dnode_, mxtl::move(olddn));
    return NO_ERROR;
}

mx_status_t VnodeDir::Link(const char* name, size_t len, fs::Vnode* target) {
    VnodeMemfs* vn = static_cast<VnodeMemfs*>(target);

    if ((len == 1) && (name[0] == '.')) {
        return ERR_BAD_STATE;
    } else if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_BAD_STATE;
    } else if (!IsDirectory()) {
        // Empty, unlinked parent
        return ERR_BAD_STATE;
    }

    if (vn->IsDirectory()) {
        // The target must not be a directory
        return ERR_NOT_FILE;
    }

    if (dnode_->Lookup(name, len, nullptr) == NO_ERROR) {
        // The destination should not exist
        return ERR_ALREADY_EXISTS;
    }

    // Make a new dnode for the new name, attach the target vnode to it
    mxtl::RefPtr<Dnode> targetdn;
    if ((targetdn = Dnode::Create(name, len, vn)) == nullptr) {
        return ERR_NO_MEMORY;
    }

    // Attach the new dnode to its parent
    Dnode::AddChild(dnode_, mxtl::move(targetdn));

    return NO_ERROR;
}

mx_status_t VnodeMemfs::Sync() {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    return NO_ERROR;
}

constexpr const char kFsName[] = "memfs";

ssize_t VnodeMemfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_DEVMGR_MOUNT_BOOTFS_VMO: {
        if (in_len < sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        const mx_handle_t* vmo = static_cast<const mx_handle_t*>(in_buf);
        return devmgr_add_systemfs_vmo(*vmo);
    }
    case IOCTL_DEVMGR_QUERY_FS: {
        if (out_len < strlen(kFsName) + 1) {
            return ERR_INVALID_ARGS;
        }
        strcpy(static_cast<char*>(out_buf), kFsName);
        return strlen(kFsName);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

mx_status_t VnodeMemfs::AttachRemote(mx_handle_t h) {
    if (!IsDirectory()) {
        return ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ERR_ALREADY_BOUND;
    }
    SetRemote(h);
    return NO_ERROR;
}

static mx_status_t memfs_create_fs(const char* name, bool device, VnodeDir** out) {
    AllocChecker ac;
    VnodeDir* fs;
    if (device) {
        fs = new (&ac) VnodeDevice();
    } else {
        fs = new (&ac) VnodeDir();
    }
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    mxtl::RefPtr<Dnode> dn = Dnode::Create(name, strlen(name), fs);
    if (dn == nullptr) {
        delete fs;
        return ERR_NO_MEMORY;
    }

    fs->dnode_ = dn; // FS root is directory
    *out = fs;
    return NO_ERROR;
}

static void memfs_mount_locked(VnodeDir* parent, VnodeDir* subtree) TA_REQ(vfs_lock) {
    Dnode::AddChild(parent->dnode_, subtree->dnode_);
}

// TODO(smklein): Update the usage of the vfs_lock here to a Vnode-specific lock,
// allowing vnode creation, but preventing TOCTTOU bugs between checking if the
// device exists and when we actually create it.
//
// precondition: no ref taken on parent
// postcondition: ref returned on out parameter
mx_status_t VnodeDir::CreateDeviceAtLocked(VnodeDir** out, const char* name,
                                           mx_handle_t h) TA_REQ(vfs_lock) {
    if (name == nullptr) {
        return ERR_INVALID_ARGS;
    }
    size_t len = strlen(name);

    // check for duplicate
    mxtl::RefPtr<Dnode> dn;
    if (dnode_->Lookup(name, len, &dn) == NO_ERROR) {
        *out = static_cast<VnodeDir*>(dn->AcquireVnode());
        if ((h == 0) && (!(*out)->IsRemote())) {
            // creating a duplicate directory node simply
            // returns the one that's already there
            return NO_ERROR;
        }
        (*out)->RefRelease();
        return ERR_ALREADY_EXISTS;
    }

    // create vnode
    mx_status_t status;
    if ((status = CanCreate(name, len)) != NO_ERROR) {
        return status;
    }
    AllocChecker ac;
    VnodeDir* vn = new (&ac) VnodeDevice();
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    if ((status = AttachVnode(vn, name, len, true)) != NO_ERROR) {
        delete vn;
        return status;
    }

    if (h) {
        // attach device
        vn->AttachRemote(h);
    }

    NotifyAdd(name, len);
    xprintf("devfs_add_node() vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

static mx_status_t memfs_add_link_locked(VnodeDir* parent, const char* name,
                                         VnodeMemfs* vn) TA_REQ(vfs_lock) {
    if ((parent == nullptr) || (vn == nullptr)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("memfs_add_link() p=%p name='%s'\n", parent, name ? name : "###");

    char tmp[8];
    size_t len;
    if (name == nullptr) {
        //TODO: something smarter
        // right now we have so few devices and instances this is not a problem
        // but it clearly is not optimal
        // seqcount is used to avoid rapidly re-using device numbers
        for (unsigned n = 0; n < 1000; n++) {
            snprintf(tmp, sizeof(tmp), "%03u", (parent->seqcount_++) % 1000);
            if (parent->dnode_->Lookup(tmp, 3, nullptr) != NO_ERROR) {
                name = tmp;
                len = 3;
                goto got_name;
            }
        }
        return ERR_ALREADY_EXISTS;
    } else {
        len = strlen(name);
        if (parent->dnode_->Lookup(name, len, nullptr) == NO_ERROR) {
            return ERR_ALREADY_EXISTS;
        }
    }
got_name:
    mxtl::RefPtr<Dnode> dn;
    if ((dn = Dnode::Create(name, len, vn)) == nullptr) {
        return ERR_NO_MEMORY;
    }
    Dnode::AddChild(parent->dnode_, mxtl::move(dn));
    parent->NotifyAdd(name, len);
    return NO_ERROR;
}

mx_status_t VnodeDir::CreateFromVmo(const char* name, size_t namelen,
                                    mx_handle_t vmo, mx_off_t off, mx_off_t len) {
    mx_status_t status;
    if ((status = CanCreate(name, namelen)) != NO_ERROR) {
        return status;
    }

    AllocChecker ac;
    VnodeMemfs* vn = new (&ac) VnodeVmo(vmo, off, len);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    if ((status = AttachVnode(vn, name, namelen, false)) != NO_ERROR) {
        delete vn;
        return status;
    }

    return NO_ERROR;
}

mx_status_t VnodeDir::CanCreate(const char* name, size_t namelen) const {
    if (!IsDirectory()) {
        return ERR_INVALID_ARGS;
    } else if (dnode_->Lookup(name, namelen, nullptr) == NO_ERROR) {
        return ERR_ALREADY_EXISTS;
    }
    return NO_ERROR;
}

mx_status_t VnodeDir::AttachVnode(VnodeMemfs* vn, const char* name, size_t namelen,
                                  bool isdir) {
    // dnode takes a reference to the vnode
    mxtl::RefPtr<Dnode> dn;
    if ((dn = Dnode::Create(name, namelen, vn)) == nullptr) { // vn refcount +1
        return ERR_NO_MEMORY;
    }
    vn->RefRelease(); // vn refcount +0

    // Identify that the vnode is a directory (vn->dnode_ != nullptr) so that
    // addding a child will also increment the parent link_count (after all,
    // directories contain a ".." entry, which is a link to their parent).
    if (isdir) {
        vn->dnode_ = dn;
    }

    // parent takes first reference
    Dnode::AddChild(dnode_, mxtl::move(dn));
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
    memfs::VnodeDir* parent = static_cast<memfs::VnodeDir*>(parent_vn);

    if (strcmp(pathout, "") == 0) {
        parent->RefRelease();
        return ERR_ALREADY_EXISTS;
    }

    fs::Vnode* out;
    r = parent->Create(&out, pathout, strlen(pathout), S_IFDIR);
    parent->RefRelease();
    if (r < 0) {
        return r;
    }
    auto vnb = static_cast<memfs::VnodeDir*>(out);
    vnb->RefRelease();
    return r;
}

memfs::VnodeDir* systemfs_get_root() {
    if (memfs::systemfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("system", false, &memfs::systemfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'system' file system\n", r);
            panic();
        }
    }
    return memfs::systemfs_root;
}

memfs::VnodeDir* memfs_get_root() {
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

memfs::VnodeDir* devfs_get_root() {
    if (memfs::devfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("dev", true, &memfs::devfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'device' file system\n", r);
            panic();
        }
    }
    return memfs::devfs_root;
}

memfs::VnodeDir* bootfs_get_root() {
    if (memfs::bootfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("boot", false, &memfs::bootfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'boot' file system\n", r);
            panic();
        }
    }
    return memfs::bootfs_root;
}

mx_status_t memfs_create_device_at(memfs::VnodeDir* parent, memfs::VnodeDir** out,
                                   const char* name, mx_handle_t h) {
    if ((parent == nullptr) || !parent->IsDirectory()) {
        return ERR_INVALID_ARGS;
    }
    mxtl::AutoLock lock(&vfs_lock);
    return parent->CreateDeviceAtLocked(out, name, h);
}

// Hardcoded initialization function to create/access global root directory
memfs::VnodeDir* vfs_create_global_root() {
    if (memfs::vfs_root == nullptr) {
        mx_status_t r = memfs_create_fs("<root>", false, &memfs::vfs_root);
        if (r < 0) {
            printf("fatal error %d allocating root file system\n", r);
            panic();
        }

        memfs_mount_locked(memfs::vfs_root, devfs_get_root());
        memfs_mount_locked(memfs::vfs_root, bootfs_get_root());
        memfs_mount_locked(memfs::vfs_root, memfs_get_root());

        memfs_create_directory("/data", 0);
        memfs_create_directory("/volume", 0);
        memfs_create_directory("/dev/socket", 0);
    }
    return memfs::vfs_root;
}

void memfs_mount(memfs::VnodeDir* parent, memfs::VnodeDir* subtree) {
    mxtl::AutoLock lock(&vfs_lock);
    memfs_mount_locked(parent, subtree);
}

mx_status_t memfs_add_link(memfs::VnodeDir* parent, const char* name,
                           memfs::VnodeMemfs* target) {
    mxtl::AutoLock lock(&vfs_lock);
    return memfs_add_link_locked(parent, name, target);
}
