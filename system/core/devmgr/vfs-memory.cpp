// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <async/loop.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/vfs.h>
#include <zircon/device/vfs.h>
#include <zircon/thread_annotations.h>
#include <fdio/debug.h>
#include <fdio/vfs.h>

#include "devmgr.h"
#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {
namespace {

Vfs root_vfs;
Vfs system_vfs;

fbl::unique_ptr<async::Loop> global_loop;

}  // namespace

// Artificially cap the maximum in-memory file size to 512MB.
constexpr size_t kMemfsMaxFileSize = 512 * 1024 * 1024;

static fbl::RefPtr<VnodeDir> global_root = nullptr;
static fbl::RefPtr<VnodeDir> memfs_root = nullptr;
static fbl::RefPtr<VnodeDir> devfs_root = nullptr;
static fbl::RefPtr<VnodeDir> bootfs_root = nullptr;
static fbl::RefPtr<VnodeDir> systemfs_root = nullptr;

static bool WindowMatchesVMO(zx_handle_t vmo, zx_off_t offset, zx_off_t length) {
    if (offset != 0)
        return false;
    uint64_t size;
    if (zx_vmo_get_size(vmo, &size) < 0)
        return false;
    return size == length;
}

zx_status_t Vfs::CreateFromVmo(VnodeDir* parent, bool vmofile, fbl::StringPiece name,
                             zx_handle_t vmo, zx_off_t off,
                             zx_off_t len) {
    fbl::AutoLock lock(&vfs_lock_);
    return parent->CreateFromVmo(vmofile, name, vmo, off, len);
}

void Vfs::MountSubtree(VnodeDir* parent, fbl::RefPtr<VnodeDir> subtree) {
    fbl::AutoLock lock(&vfs_lock_);
    parent->MountSubtree(fbl::move(subtree));
}

fbl::atomic<uint64_t> VnodeMemfs::ino_ctr_(0);

VnodeMemfs::VnodeMemfs(Vfs* vfs) : dnode_(nullptr), link_count_(0), vfs_(vfs),
    ino_(ino_ctr_.fetch_add(1, fbl::memory_order_relaxed)) {
    create_time_ = modify_time_ = zx_time_get(ZX_CLOCK_UTC);
}

VnodeMemfs::~VnodeMemfs() {}

VnodeFile::VnodeFile(Vfs* vfs)
    : VnodeMemfs(vfs), vmo_(ZX_HANDLE_INVALID), length_(0) {}

VnodeFile::VnodeFile(Vfs* vfs, zx_handle_t vmo, zx_off_t length)
    : VnodeMemfs(vfs), vmo_(vmo), length_(length) {}

VnodeFile::~VnodeFile() {
    if (vmo_ != ZX_HANDLE_INVALID) {
        zx_handle_close(vmo_);
    }
}

VnodeDir::VnodeDir(Vfs* vfs) : VnodeMemfs(vfs) {
    link_count_ = 1; // Implied '.'
}
VnodeDir::~VnodeDir() {}

VnodeVmo::VnodeVmo(Vfs* vfs, zx_handle_t vmo, zx_off_t offset, zx_off_t length)
    : VnodeMemfs(vfs), vmo_(vmo), offset_(offset), length_(length), have_local_clone_(false) {}

VnodeVmo::~VnodeVmo() {
    if (have_local_clone_) {
        zx_handle_close(vmo_);
    }
}

zx_status_t VnodeDir::ValidateFlags(uint32_t flags) {
    switch (flags & O_ACCMODE) {
    case O_WRONLY:
    case O_RDWR:
        return ZX_ERR_NOT_FILE;
    }
    return ZX_OK;
}

zx_status_t VnodeFile::ValidateFlags(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t VnodeVmo::ValidateFlags(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    switch (flags & O_ACCMODE) {
        case O_WRONLY:
        case O_RDWR:
            return ZX_ERR_ACCESS_DENIED;
        }
        return ZX_OK;
    }

zx_status_t VnodeVmo::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return ZX_OK;
}

zx_status_t VnodeVmo::GetHandles(uint32_t flags, zx_handle_t* hnds, size_t* hcount,
                                 uint32_t* type, void* extra, uint32_t* esize) {
    zx_off_t* off = static_cast<zx_off_t*>(extra);
    zx_off_t* len = off + 1;
    zx_handle_t vmo;
    zx_status_t status;
    if (!have_local_clone_ && !WindowMatchesVMO(vmo_, offset_, length_)) {
        status = zx_vmo_clone(vmo_, ZX_VMO_CLONE_COPY_ON_WRITE, offset_, length_, &vmo_);
        if (status < 0)
            return status;
        offset_ = 0;
        have_local_clone_ = true;
    }
    status = zx_handle_duplicate(
        vmo_,
        ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP |
        ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status < 0)
        return status;
    xprintf("vmofile: %x (%x) off=%" PRIu64 " len=%" PRIu64 "\n", vmo, vmo_, offset_, length_);

    *off = offset_;
    *len = length_;
    hnds[0] = vmo;
    *type = FDIO_PROTOCOL_VMOFILE;
    *esize = sizeof(zx_off_t) * 2;
    *hcount = 1;
    return ZX_OK;
}

zx_status_t VnodeFile::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    if ((off >= length_) || (vmo_ == ZX_HANDLE_INVALID)) {
        *out_actual = 0;
        return ZX_OK;
    } else if (len > length_ - off) {
        len = length_ - off;
    }

    return zx_vmo_read(vmo_, data, off, len, out_actual);
}

zx_status_t VnodeVmo::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    if (off > length_) {
        *out_actual = 0;
        return ZX_OK;
    }
    size_t rlen = length_ - off;
    if (len > rlen)
        len = rlen;
    return zx_vmo_read(vmo_, data, offset_ + off, len, out_actual);
}

zx_status_t VnodeFile::Write(const void* data, size_t len, size_t offset,
                             size_t* out_actual) {
    zx_status_t status;
    size_t newlen = offset + len;
    newlen = newlen > kMemfsMaxFileSize ? kMemfsMaxFileSize : newlen;
    size_t alignedLen = fbl::round_up(newlen, static_cast<size_t>(PAGE_SIZE));

    if (vmo_ == ZX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = zx_vmo_create(alignedLen, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    } else if (newlen > fbl::round_up(length_, static_cast<size_t>(PAGE_SIZE))) {
        // Accessing beyond the end of the file? Extend it.
        if ((status = zx_vmo_set_size(vmo_, alignedLen)) != ZX_OK) {
            return status;
        }
    }

    if ((status = zx_vmo_write(vmo_, data, offset, len, out_actual)) != ZX_OK) {
        return status;
    }

    if (newlen > length_) {
        length_ = newlen;
    }
    if (*out_actual == 0 && offset >= kMemfsMaxFileSize) {
        // short write because we're beyond the end of the permissible length
        return ZX_ERR_FILE_BIG;
    }
    UpdateModified();
    return ZX_OK;
}

zx_status_t VnodeFile::Append(const void* data, size_t len, size_t* out_end,
                              size_t* out_actual) {
    zx_status_t status = Write(data, len, length_, out_actual);
    *out_end = length_;
    return status;
}

zx_status_t VnodeFile::Mmap(int flags, size_t len, size_t* off, zx_handle_t* out) {
    if (vmo_ == ZX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        zx_status_t status;
        if ((status = zx_vmo_create(0, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    }

    zx_rights_t rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP;
    rights |= (flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
    rights |= (flags & FDIO_MMAP_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
    rights |= (flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
    if (flags & FDIO_MMAP_FLAG_PRIVATE) {
        return zx_vmo_clone(vmo_, ZX_VMO_CLONE_COPY_ON_WRITE, 0, length_, out);
    }

    return zx_handle_duplicate(vmo_, rights, out);
}

zx_status_t VnodeDir::Mmap(int flags, size_t len, size_t* off, zx_handle_t* out) {
    return ZX_ERR_ACCESS_DENIED;
}

bool VnodeDir::IsRemote() const { return remoter_.IsRemote(); }
zx::channel VnodeDir::DetachRemote() { return remoter_.DetachRemote(); }
zx_handle_t VnodeDir::GetRemote() const { return remoter_.GetRemote(); }
void VnodeDir::SetRemote(zx::channel remote) { return remoter_.SetRemote(fbl::move(remote)); }

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_FOUND;
    }
    fbl::RefPtr<Dnode> dn;
    zx_status_t r = dnode_->Lookup(name, &dn);
    ZX_DEBUG_ASSERT(r <= 0);
    if (r == ZX_OK) {
        if (dn == nullptr) {
            // Looking up our own vnode
            *out = fbl::RefPtr<VnodeDir>(this);
        } else {
            // Looking up a different vnode
            *out = dn->AcquireVnode();
        }
    }
    return r;
}

constexpr uint64_t kMemfsBlksize = PAGE_SIZE;

zx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
    attr->size = length_;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

zx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->size = 0;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

zx_status_t VnodeVmo::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

zx_status_t VnodeMemfs::Setattr(const vnattr_t* attr) {
    if ((attr->valid & ~(ATTR_MTIME)) != 0) {
        // only attr currently supported
        return ZX_ERR_INVALID_ARGS;
    }
    if (attr->valid & ATTR_MTIME) {
        modify_time_ = attr->modify_time;
    }
    return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::vdircookie_t* cookie, void* data, size_t len, size_t* out_actual) {
    fs::DirentFiller df(data, len);
    if (!IsDirectory()) {
        // This WAS a directory, but it has been deleted.
        Dnode::ReaddirStart(&df, cookie);
        *out_actual = df.BytesFilled();
        return ZX_OK;
    }
    dnode_->Readdir(&df, cookie);
    *out_actual = df.BytesFilled();
    return ZX_OK;
}

// postcondition: reference taken on vn returned through "out"
zx_status_t VnodeDir::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    zx_status_t status;
    if ((status = CanCreate(name)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<memfs::VnodeMemfs> vn;
    if (S_ISDIR(mode)) {
        vn = fbl::AdoptRef(new (&ac) memfs::VnodeDir(vfs()));
    } else {
        vn = fbl::AdoptRef(new (&ac) memfs::VnodeFile(vfs()));
    }

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = AttachVnode(vn, name, S_ISDIR(mode))) != ZX_OK) {
        return status;
    }
    *out = fbl::move(vn);
    return status;
}

zx_status_t VnodeDir::Unlink(fbl::StringPiece name, bool must_be_dir) {
    if (!IsDirectory()) {
        // Calling unlink from unlinked, empty directory
        return ZX_ERR_BAD_STATE;
    }
    fbl::RefPtr<Dnode> dn;
    zx_status_t r;
    if ((r = dnode_->Lookup(name, &dn)) != ZX_OK) {
        return r;
    } else if (dn == nullptr) {
        // Cannot unlink directory 'foo' using the argument 'foo/.'
        return ZX_ERR_UNAVAILABLE;
    } else if (!dn->IsDirectory() && must_be_dir) {
        // Path ending in "/" was requested, implying that the dnode must be a directory
        return ZX_ERR_NOT_DIR;
    } else if ((r = dn->CanUnlink()) != ZX_OK) {
        return r;
    }

    dn->Detach();
    return ZX_OK;
}

zx_status_t VnodeFile::Truncate(size_t len) {
    zx_status_t status;
    if (len > kMemfsMaxFileSize) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t alignedLen = fbl::round_up(len, static_cast<size_t>(PAGE_SIZE));

    if (vmo_ == ZX_HANDLE_INVALID) {
        // First access to the file? Allocate it.
        if ((status = zx_vmo_create(alignedLen, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    } else if ((len < length_) && (len % PAGE_SIZE != 0)) {
        // Currently, if the file is truncated to a 'partial page', an later re-expanded, then the
        // partial page is *not necessarily* filled with zeroes. As a consequence, we manually must
        // fill the portion between "len" and the next highest page (or vn->length, whichever
        // is smaller) with zeroes.
        char buf[PAGE_SIZE];
        size_t ppage_size = PAGE_SIZE - (len % PAGE_SIZE);
        ppage_size = len + ppage_size < length_ ? ppage_size : length_ - len;
        memset(buf, 0, ppage_size);
        size_t actual;
        status = zx_vmo_write(vmo_, buf, len, ppage_size, &actual);
        if ((status != ZX_OK) || (actual != ppage_size)) {
            return status != ZX_OK ? ZX_ERR_IO : status;
        } else if ((status = zx_vmo_set_size(vmo_, alignedLen)) != ZX_OK) {
            return status;
        }
    } else if ((status = zx_vmo_set_size(vmo_, alignedLen)) != ZX_OK) {
        return status;
    }

    length_ = len;
    modify_time_ = zx_time_get(ZX_CLOCK_UTC);
    return ZX_OK;
}

zx_status_t VnodeDir::Rename(fbl::RefPtr<fs::Vnode> _newdir, fbl::StringPiece oldname,
                             fbl::StringPiece newname, bool src_must_be_dir,
                             bool dst_must_be_dir) {
    auto newdir = fbl::RefPtr<VnodeMemfs>::Downcast(fbl::move(_newdir));

    if (!IsDirectory() || !newdir->IsDirectory())
        return ZX_ERR_BAD_STATE;

    fbl::RefPtr<Dnode> olddn;
    zx_status_t r;
    // The source must exist
    if ((r = dnode_->Lookup(oldname, &olddn)) != ZX_OK) {
        return r;
    }
    ZX_DEBUG_ASSERT(olddn != nullptr);

    if (!olddn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return ZX_ERR_NOT_DIR;
    }

    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (olddn->IsSubdirectory(newdir->dnode_)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // The destination may or may not exist
    fbl::RefPtr<Dnode> targetdn;
    r = newdir->dnode_->Lookup(newname, &targetdn);
    bool target_exists = (r == ZX_OK);
    if (target_exists) {
        ZX_DEBUG_ASSERT(targetdn != nullptr);
        // The target exists. Validate and unlink it.
        if (olddn == targetdn) {
            // Cannot rename node to itself
            return ZX_ERR_INVALID_ARGS;
        }
        if (olddn->IsDirectory() != targetdn->IsDirectory()) {
            // Cannot rename files to directories (and vice versa)
            return ZX_ERR_INVALID_ARGS;
        } else if ((r = targetdn->CanUnlink()) != ZX_OK) {
            return r;
        }
    } else if (r != ZX_ERR_NOT_FOUND) {
        return r;
    }

    // Allocate the new name for the dnode, either by
    // (1) Stealing it from the previous dnode, if it used the same name, or
    // (2) Allocating a new name, if creating a new name.
    fbl::unique_ptr<char[]> namebuffer(nullptr);
    if (target_exists) {
        targetdn->Detach();
        namebuffer = fbl::move(targetdn->TakeName());
    } else {
        fbl::AllocChecker ac;
        namebuffer.reset(new (&ac) char[newname.length() + 1]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(namebuffer.get(), newname.data(), newname.length());
        namebuffer[newname.length()] = '\0';
    }

    // NOTE:
    //
    // Validation ends here, and modifications begin. Rename should not fail
    // beyond this point.

    olddn->RemoveFromParent();
    olddn->PutName(fbl::move(namebuffer), newname.length());
    Dnode::AddChild(newdir->dnode_, fbl::move(olddn));
    return ZX_OK;
}

zx_status_t VnodeDir::Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) {
    auto vn = fbl::RefPtr<VnodeMemfs>::Downcast(fbl::move(target));

    if (!IsDirectory()) {
        // Empty, unlinked parent
        return ZX_ERR_BAD_STATE;
    }

    if (vn->IsDirectory()) {
        // The target must not be a directory
        return ZX_ERR_NOT_FILE;
    }

    if (dnode_->Lookup(name, nullptr) == ZX_OK) {
        // The destination should not exist
        return ZX_ERR_ALREADY_EXISTS;
    }

    // Make a new dnode for the new name, attach the target vnode to it
    fbl::RefPtr<Dnode> targetdn;
    if ((targetdn = Dnode::Create(name, vn)) == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    // Attach the new dnode to its parent
    Dnode::AddChild(dnode_, fbl::move(targetdn));

    return ZX_OK;
}

zx_status_t VnodeMemfs::Sync() {
    // Since this filesystem is in-memory, all data is already up-to-date in
    // the underlying storage
    return ZX_OK;
}

constexpr const char kFsName[] = "memfs";

zx_status_t VnodeMemfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                              void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_VFS_MOUNT_BOOTFS_VMO: {
        if (in_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const zx_handle_t* vmo = static_cast<const zx_handle_t*>(in_buf);
        ssize_t r = devmgr_add_systemfs_vmo(*vmo);
        if (r < 0) {
            return zx_status_t(r);
        }
        *out_actual = static_cast<size_t>(r);
        return ZX_OK;
    }
    case IOCTL_VFS_QUERY_FS: {
        if (out_len < sizeof(vfs_query_info_t) + strlen(kFsName)) {
            return ZX_ERR_INVALID_ARGS;
        }

        vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
        memset(info, 0, sizeof(*info));
        //TODO(planders): eventually report something besides 0.
        memcpy(info->name, kFsName, strlen(kFsName));
        *out_actual = sizeof(vfs_query_info_t) + strlen(kFsName);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t VnodeDir::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_VFS_VMO_CREATE: {
        const auto* config = reinterpret_cast<const vmo_create_config_t*>(in_buf);
        size_t namelen = in_len - sizeof(vmo_create_config_t) - 1;
        fbl::StringPiece name(config->name, namelen);
        if (in_len <= sizeof(vmo_create_config_t) || (namelen > NAME_MAX) ||
            (name[namelen] != 0)) {
            zx_handle_close(config->vmo);
            return ZX_ERR_INVALID_ARGS;
        }

        // Ensure this is the last handle to this VMO; otherwise, the size
        // may change from underneath us.
        zx_info_handle_count_t info;
        zx_status_t status = zx_object_get_info(config->vmo, ZX_INFO_HANDLE_COUNT,
                                                &info, sizeof(info), nullptr, nullptr);
        if (status != ZX_OK || info.handle_count != 1) {
            zx_handle_close(config->vmo);
            return ZX_ERR_INVALID_ARGS;
        }

        uint64_t size;
        if ((status = zx_vmo_get_size(config->vmo, &size)) != ZX_OK) {
            zx_handle_close(config->vmo);
            return status;
        }

        bool vmofile = false;
        *out_actual = 0;
        return vfs()->CreateFromVmo(this, vmofile, name, config->vmo, 0, size);
    }
    default:
        return VnodeMemfs::Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    }
}

zx_status_t VnodeMemfs::AttachRemote(fs::MountChannel h) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    SetRemote(fbl::move(h.TakeChannel()));
    return ZX_OK;
}

static zx_status_t memfs_create_fs(const char* name, memfs::Vfs* vfs, fbl::RefPtr<VnodeDir>* out) {
    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeDir> fs = fbl::AdoptRef(new (&ac) VnodeDir(vfs));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::RefPtr<Dnode> dn = Dnode::Create(name, fs);
    if (dn == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    fs->dnode_ = dn; // FS root is directory
    *out = fs;
    return ZX_OK;
}

void VnodeDir::MountSubtree(fbl::RefPtr<VnodeDir> subtree) {
    Dnode::AddChild(dnode_, subtree->dnode_);
}

zx_status_t VnodeDir::CreateFromVmo(bool vmofile, fbl::StringPiece name,
                                    zx_handle_t vmo, zx_off_t off, zx_off_t len) {
    zx_status_t status;
    if ((status = CanCreate(name)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeMemfs> vn;
    if (vmofile) {
        vn = fbl::AdoptRef(new (&ac) VnodeVmo(vfs(), vmo, off, len));
    } else {
        vn = fbl::AdoptRef(new (&ac) VnodeFile(vfs(), vmo, len));
    }
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    if ((status = AttachVnode(fbl::move(vn), name, false)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

zx_status_t VnodeDir::CanCreate(fbl::StringPiece name) const {
    if (!IsDirectory()) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status;
    if ((status = dnode_->Lookup(name, nullptr)) == ZX_ERR_NOT_FOUND) {
        return ZX_OK;
    } else if (status == ZX_OK) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    return status;
}

zx_status_t VnodeDir::AttachVnode(fbl::RefPtr<VnodeMemfs> vn, fbl::StringPiece name,
                                  bool isdir) {
    // dnode takes a reference to the vnode
    fbl::RefPtr<Dnode> dn;
    if ((dn = Dnode::Create(name, vn)) == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    // Identify that the vnode is a directory (vn->dnode_ != nullptr) so that
    // addding a child will also increment the parent link_count (after all,
    // directories contain a ".." entry, which is a link to their parent).
    if (isdir) {
        vn->dnode_ = dn;
    }

    // parent takes first reference
    Dnode::AddChild(dnode_, fbl::move(dn));
    return ZX_OK;
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they can
// be exposed to C:

fbl::RefPtr<memfs::VnodeDir> SystemfsRoot() {
    if (memfs::systemfs_root == nullptr) {
        zx_status_t r = memfs_create_fs("system", &memfs::system_vfs, &memfs::systemfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'system' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::systemfs_root;
}

fbl::RefPtr<memfs::VnodeDir> MemfsRoot() {
    if (memfs::memfs_root == nullptr) {
        zx_status_t r = memfs_create_fs("tmp", &memfs::root_vfs, &memfs::memfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'tmp' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::memfs_root;
}

fbl::RefPtr<memfs::VnodeDir> DevfsRoot() {
    if (memfs::devfs_root == nullptr) {
        zx_status_t r = memfs_create_fs("dev", &memfs::root_vfs, &memfs::devfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'device' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::devfs_root;
}

fbl::RefPtr<memfs::VnodeDir> BootfsRoot() {
    if (memfs::bootfs_root == nullptr) {
        zx_status_t r = memfs_create_fs("boot", &memfs::root_vfs, &memfs::bootfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'boot' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::bootfs_root;
}

zx_status_t devfs_mount(zx_handle_t h) {
    return DevfsRoot()->AttachRemote(fs::MountChannel(h));
}

VnodeDir* systemfs_get_root() {
    return SystemfsRoot().get();
}

void systemfs_set_readonly(bool value) {
    SystemfsRoot()->vfs()->SetReadonly(value);
}

static zx_status_t add_vmofile(fbl::RefPtr<VnodeDir> vnb, const char* path, zx_handle_t vmo,
                        zx_off_t off, size_t len) {
    zx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ZX_ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == nullptr) {
            if (path[0] == 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            bool vmofile = true;
            return vnb->vfs()->CreateFromVmo(vnb.get(), vmofile,
                                             fbl::StringPiece(path, strlen(path)), vmo, off, len);
        } else {
            if (nextpath == path) {
                return ZX_ERR_INVALID_ARGS;
            }

            fbl::RefPtr<fs::Vnode> out;
            r = vnb->Lookup(&out, fbl::StringPiece(path, nextpath - path));
            if (r == ZX_ERR_NOT_FOUND) {
                r = vnb->Create(&out, fbl::StringPiece(path, nextpath - path), S_IFDIR);
            }

            if (r < 0) {
                return r;
            }
            vnb = fbl::RefPtr<VnodeDir>::Downcast(fbl::move(out));
            path = nextpath + 1;
        }
    }
}

zx_status_t bootfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_vmofile(BootfsRoot(), path, vmo, off, len);
}

zx_status_t systemfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_vmofile(SystemfsRoot(), path, vmo, off, len);
}

// Hardcoded initialization function to create/access global root directory
VnodeDir* vfs_create_global_root() {
    if (memfs::global_root == nullptr) {
        zx_status_t r = memfs_create_fs("<root>", &memfs::root_vfs, &memfs::global_root);
        if (r < 0) {
            printf("fatal error %d allocating root file system\n", r);
            __builtin_trap();
        }

        memfs::root_vfs.MountSubtree(memfs::global_root.get(), DevfsRoot());
        memfs::root_vfs.MountSubtree(memfs::global_root.get(), BootfsRoot());
        memfs::root_vfs.MountSubtree(memfs::global_root.get(), MemfsRoot());

        fbl::RefPtr<fs::Vnode> vn;
        fbl::StringPiece pathout;
        ZX_ASSERT(memfs::root_vfs.Open(memfs::global_root, &vn, fbl::StringPiece("/data"), &pathout,
                                       O_CREAT, S_IFDIR) == ZX_OK);
        ZX_ASSERT(memfs::root_vfs.Open(memfs::global_root, &vn, fbl::StringPiece("/volume"), &pathout,
                                       O_CREAT, S_IFDIR) == ZX_OK);

        memfs::global_loop.reset(new async::Loop());
        memfs::global_loop->StartThread("root-dispatcher");
        memfs::root_vfs.set_async(memfs::global_loop->async());
        memfs::system_vfs.set_async(memfs::global_loop->async());
    }
    return memfs::global_root.get();
}

void devmgr_vfs_exit() {
    memfs::root_vfs.UninstallAll(zx_deadline_after(ZX_SEC(5)));
    memfs::system_vfs.UninstallAll(zx_deadline_after(ZX_SEC(5)));
}

zx_status_t memfs_mount(VnodeDir* parent, const char* name, VnodeDir* subtree) {
    fbl::RefPtr<fs::Vnode> vn;
    zx_status_t status = parent->Create(&vn, fbl::StringPiece(name), S_IFDIR);
    if (status != ZX_OK)
        return status;
    zx_handle_t h = vfs_create_root_handle(subtree);
    // TODO(abarth): vfs_create_root_handle can fail but has no way to report
    // its failures now that zx_handle_t is unsigned.
    return parent->vfs()->InstallRemote(fbl::move(vn), fs::MountChannel(h));
}

// Acquire the root vnode and return a handle to it through the VFS dispatcher
zx_handle_t vfs_create_root_handle(VnodeMemfs* vn) {
    zx_status_t r;
    zx::channel h1, h2;
    if ((r = zx::channel::create(0, &h1, &h2)) != ZX_OK) {
        return r;
    }
    if ((r = vn->vfs()->ServeDirectory(fbl::RefPtr<fs::Vnode>(vn),
                                       fbl::move(h1))) != ZX_OK) {
        return r;
    }
    return h2.release();
}

zx_status_t vfs_connect_root_handle(VnodeMemfs* vn, zx_handle_t h) {
    zx::channel ch(h);
    return vn->vfs()->ServeDirectory(fbl::RefPtr<fs::Vnode>(vn), fbl::move(ch));
}
