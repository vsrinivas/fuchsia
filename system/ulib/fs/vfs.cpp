// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mxio/remoteio.h>
#include <mxtl/auto_call.h>

#ifdef __Fuchsia__
#include <threads.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <mxtl/auto_lock.h>
#endif

#include "vfs-internal.h"

uint32_t __trace_bits;

#ifdef __Fuchsia__
mtx_t vfs_lock = MTX_INIT;
#endif

namespace fs {
namespace {

// Trim a name before sending it to internal filesystem functions.
// Trailing '/' characters imply that the name must refer to a directory.
mx_status_t vfs_name_trim(const char* name, size_t len, size_t* len_out, bool* dir_out) {
    bool is_dir = false;
    while ((len > 0) && name[len - 1] == '/') {
        len--;
        is_dir = true;
    }

    // 'name' should not contain paths consisting of exclusively '/' characters.
    if (len == 0) {
        return ERR_INVALID_ARGS;
    } else if (len > NAME_MAX) {
        return ERR_BAD_PATH;
    }

    *len_out = len;
    *dir_out = is_dir;
    return NO_ERROR;
}

} // namespace anonymous

bool RemoteContainer::IsRemote() const {
    return remote_ > 0;
}

mx_handle_t RemoteContainer::DetachRemote(uint32_t &flags_) {
    mx_handle_t h = remote_;
    remote_ = MX_HANDLE_INVALID;
    flags_ &= ~V_FLAG_MOUNT_READY;
    return h;
}

// Access the remote handle if it's ready -- otherwise, return an error.
mx_handle_t RemoteContainer::WaitForRemote(uint32_t &flags_) {
#ifdef __Fuchsia__
    if (remote_ == 0) {
        // Trying to get remote on a non-remote vnode
        return ERR_UNAVAILABLE;
    } else if (!(flags_ & V_FLAG_MOUNT_READY)) {
        mx_signals_t observed;
        mx_status_t status = mx_object_wait_one(remote_,
                                                MX_USER_SIGNAL_0 | MX_CHANNEL_PEER_CLOSED,
                                                0,
                                                &observed);
        if ((status != NO_ERROR) || (observed & MX_CHANNEL_PEER_CLOSED)) {
            // Not set (or otherwise remote is bad)
            return ERR_UNAVAILABLE;
        }
        flags_ |= V_FLAG_MOUNT_READY;
    }
    return remote_;
#else
    return ERR_NOT_SUPPORTED;
#endif
}

mx_handle_t RemoteContainer::GetRemote() const {
    return remote_;
}

void RemoteContainer::SetRemote(mx_handle_t remote) {
    remote_ = remote;
}

mx_status_t Vfs::Open(mxtl::RefPtr<Vnode> vndir, mxtl::RefPtr<Vnode>* out, const char* path,
                      const char** pathout, uint32_t flags, uint32_t mode) {
    trace(VFS, "VfsOpen: path='%s' flags=%d\n", path, flags);
    mx_status_t r;
    if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
    if (r > 0) {
        // remote filesystem, return handle and path through to caller
        *pathout = path;
        return r;
    }

    size_t len = strlen(path);
    mxtl::RefPtr<Vnode> vn;

    bool must_be_dir = false;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != NO_ERROR) {
        return r;
    }

    if (flags & O_CREAT) {
        if (must_be_dir && !S_ISDIR(mode)) {
            return ERR_INVALID_ARGS;
        }
        if ((r = vndir->Create(&vn, path, len, mode)) < 0) {
            if ((r == ERR_ALREADY_EXISTS) && (!(flags & O_EXCL))) {
                goto try_open;
            }
            if (r == ERR_NOT_SUPPORTED) {
                // filesystem may not supporte create (like devfs)
                // in which case we should still try to open() the file
                goto try_open;
            }
            return r;
        }
        vndir->NotifyAdd(path, len);
    } else {
    try_open:
        r = vndir->Lookup(&vn, path, len);
        if (r < 0) {
            return r;
        }
        if (!(flags & O_NOREMOTE) && vn->IsRemote() && !vn->IsDevice()) {
            // Opening a mount point: Traverse across remote.
            // Devices are different, even though they also have remotes.  Ignore them.
            *pathout = ".";
            r = vn->WaitForRemote();
            return r;
        }

#ifdef __Fuchsia__
        flags |= (must_be_dir ? O_DIRECTORY : 0);
#endif
        if ((r = vn->Open(flags)) < 0) {
            return r;
        }
        if (vn->IsDevice() && !(flags & O_DIRECTORY)) {
            *pathout = ".";
            r = vn->GetRemote();
            return r;
        }
        if ((flags & O_TRUNC) && ((r = vn->Truncate(0)) < 0)) {
            return r;
        }
    }
    trace(VFS, "VfsOpen: vn=%p\n", vn.get());
    *pathout = "";
    *out = vn;
    return NO_ERROR;
}

mx_status_t Vfs::Unlink(mxtl::RefPtr<Vnode> vndir, const char* path, size_t len) {
    bool must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != NO_ERROR) {
        return r;
    }
    return vndir->Unlink(path, len, must_be_dir);
}

mx_status_t Vfs::Link(mxtl::RefPtr<Vnode> oldparent, mxtl::RefPtr<Vnode> newparent,
                      const char* oldname, const char* newname) {
    // Local filesystem
    size_t oldlen = strlen(oldname);
    size_t newlen = strlen(newname);
    bool old_must_be_dir;
    bool new_must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(oldname, oldlen, &oldlen, &old_must_be_dir)) != NO_ERROR) {
        return r;
    } else if (old_must_be_dir) {
        return ERR_NOT_DIR;
    }

    if ((r = vfs_name_trim(newname, newlen, &newlen, &new_must_be_dir)) != NO_ERROR) {
        return r;
    } else if (new_must_be_dir) {
        return ERR_NOT_DIR;
    }

    // Look up the target vnode
    mxtl::RefPtr<Vnode> target;
    if ((r = oldparent->Lookup(&target, oldname, oldlen)) < 0) {
        return r;
    }
    r = newparent->Link(newname, newlen, target);
    if (r != NO_ERROR) {
        return r;
    }
    newparent->NotifyAdd(newname, newlen);
    return NO_ERROR;
}

mx_status_t Vfs::Rename(mxtl::RefPtr<Vnode> oldparent, mxtl::RefPtr<Vnode> newparent,
                        const char* oldname, const char* newname) {
    // Local filesystem
    size_t oldlen = strlen(oldname);
    size_t newlen = strlen(newname);
    bool old_must_be_dir;
    bool new_must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(oldname, oldlen, &oldlen, &old_must_be_dir)) != NO_ERROR) {
        return r;
    }
    if ((r = vfs_name_trim(newname, newlen, &newlen, &new_must_be_dir)) != NO_ERROR) {
        return r;
    }
    r = oldparent->Rename(newparent, oldname, oldlen, newname, newlen,
                          old_must_be_dir, new_must_be_dir);
    if (r != NO_ERROR) {
        return r;
    }
    newparent->NotifyAdd(newname, newlen);
    return NO_ERROR;
}

ssize_t Vfs::Ioctl(mxtl::RefPtr<Vnode> vn, uint32_t op, const void* in_buf, size_t in_len,
                   void* out_buf, size_t out_len) {
    switch (op) {
#ifdef __Fuchsia__
    case IOCTL_VFS_WATCH_DIR: {
        if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        mx_status_t status = vn->WatchDir(reinterpret_cast<mx_handle_t*>(out_buf));
        if (status != NO_ERROR) {
            return status;
        }
        return sizeof(mx_handle_t);
    }
    case IOCTL_VFS_MOUNT_FS: {
        if ((in_len != sizeof(mx_handle_t)) || (out_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h = *reinterpret_cast<const mx_handle_t*>(in_buf);
        mx_status_t status = Vfs::InstallRemote(vn, h);
        if (status < 0) {
            // If we can't install the filesystem, we shoot off a quick "unmount"
            // signal to the filesystem process, since we are the owner of its
            // root handle.
            // TODO(smklein): Transfer the mountpoint back to the caller on error,
            // so they can decide what to do with it.
            vfs_unmount_handle(h, 0);
            mx_handle_close(h);
        }
        return status;
    }
    case IOCTL_VFS_MOUNT_MKDIR_FS: {
        size_t namelen = in_len - sizeof(mount_mkdir_config_t);
        const mount_mkdir_config_t* config = reinterpret_cast<const mount_mkdir_config_t*>(in_buf);
        const char* name = config->name;
        if ((in_len < sizeof(mount_mkdir_config_t)) ||
            (namelen < 1) || (namelen > PATH_MAX) || (name[namelen - 1] != 0) ||
            (out_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        mxtl::AutoLock lock(&vfs_lock);
        mx_status_t r = Open(vn, &vn, name, &name,
                             O_CREAT | O_RDONLY | O_DIRECTORY | O_NOREMOTE, S_IFDIR);
        MX_DEBUG_ASSERT(r <= NO_ERROR); // Should not be accessing remote nodes
        if (r < 0) {
            return r;
        }
        if (vn->IsRemote()) {
            if (config->flags & MOUNT_MKDIR_FLAG_REPLACE) {
                // There is an old remote handle on this vnode; shut it down and
                // replace it with our own.
                mx_handle_t old_remote;
                Vfs::UninstallRemote(vn, &old_remote);
                vfs_unmount_handle(old_remote, 0);
                mx_handle_close(old_remote);
            } else {
                return ERR_BAD_STATE;
            }
        }
        // Lock already held; don't lock again
        mx_handle_t h = config->fs_root;
        mx_status_t status = Vfs::InstallRemoteLocked(vn, h);
        if (status < 0) {
            // If we can't install the filesystem, we shoot off a quick "unmount"
            // signal to the filesystem process, since we are the owner of its
            // root handle.
            // TODO(smklein): Transfer the mountpoint back to the caller on error,
            // so they can decide what to do with it.
            vfs_unmount_handle(h, 0);
            mx_handle_close(h);
        }
        return status;
    }
    case IOCTL_VFS_UNMOUNT_NODE: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t* h = (mx_handle_t*)out_buf;
        return Vfs::UninstallRemote(vn, h);
    }
    case IOCTL_VFS_UNMOUNT_FS: {
        vfs_uninstall_all(MX_TIME_INFINITE);
        vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
        exit(0);
    }
#endif
    default:
        return vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
    }
}

mx_status_t Vnode::Close() {
    return NO_ERROR;
}

DirentFiller::DirentFiller(void* ptr, size_t len) :
    ptr_(static_cast<char*>(ptr)), pos_(0), len_(len) {}

mx_status_t DirentFiller::Next(const char* name, size_t len, uint32_t type) {
    vdirent_t* de = reinterpret_cast<vdirent_t*>(ptr_ + pos_);
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3) {
        sz = (sz + 3) & (~3);
    }
    if (sz > len_ - pos_) {
        return ERR_INVALID_ARGS;
    }
    de->size = static_cast<uint32_t>(sz);
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    pos_ += sz;
    return NO_ERROR;
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
//
// If a non-negative status is returned, the vnode at 'out' has been acquired.
// Otherwise, no net deltas in acquires/releases occur.
mx_status_t Vfs::Walk(mxtl::RefPtr<Vnode> vn, mxtl::RefPtr<Vnode>* out,
                      const char* path, const char** pathout) {
    mx_status_t r;

    for (;;) {
        while (path[0] == '/') {
            // discard extra leading /s
            path++;
        }
        if (path[0] == 0) {
            // convert empty initial path of final path segment to "."
            path = ".";
        }
        if (vn->IsRemote() && !vn->IsDevice()) {
            // remote filesystem mount, caller must resolve
            // devices are different, so ignore them even though they can have vn->remote
            if ((r = vn->WaitForRemote()) < 0) {
                return r;
            }
            *out = vn;
            *pathout = path;
            return r;
        }

        const char* nextpath = strchr(path, '/');
        bool additional_segment = false;
        if (nextpath != nullptr) {
            const char* end = nextpath;
            while (*end != '\0') {
                if (*end != '/') {
                    additional_segment = true;
                    break;
                }
                end++;
            }
        }
        if (additional_segment) {
            // path has at least one additional segment
            // traverse to the next segment
            size_t len = nextpath - path;
            nextpath++;
            r = vn->Lookup(&vn, path, len);
            assert(r <= 0);
            if (r < 0) {
                return r;
            }
            path = nextpath;
        } else {
            // final path segment, we're done here
            *out = vn;
            *pathout = path;
            return NO_ERROR;
        }
    }
}

} // namespace fs
