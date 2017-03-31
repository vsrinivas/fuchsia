// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#endif

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vfs-internal.h"

uint32_t __trace_bits;

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

// Access the remote handle if it's ready -- otherwise, return an error.
mx_handle_t Vnode::WaitForRemote() {
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

mx_status_t Vfs::Open(Vnode* vndir, Vnode** out, const char* path,
                      const char** pathout, uint32_t flags, uint32_t mode) {
    trace(VFS, "VfsOpen: path='%s' flags=%d\n", path, flags);
    mx_status_t r;
    if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
    if (r > 0) {
        // remote filesystem, return handle and path through to caller
        vndir->RefRelease();
        *pathout = path;
        return r;
    }

    if ((flags & O_CREAT) && (flags & O_NOREMOTE)) {
        return ERR_INVALID_ARGS;
    }

    size_t len = strlen(path);
    Vnode* vn;

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
            vndir->RefRelease();
            return r;
        } else {
            vndir->RefRelease();
        }
    } else {
    try_open:
        r = vndir->Lookup(&vn, path, len);
        vndir->RefRelease();
        if (r < 0) {
            return r;
        }
        if (flags & O_NOREMOTE) {
            // Opening a mount point: Do NOT traverse across remote.
            if (!vn->IsRemote()) {
                // There must be a remote handle mounted on this vnode.
                vn->RefRelease();
                return ERR_BAD_STATE;
            }
        } else if (vn->IsRemote() && !vn->IsDevice()) {
            // Opening a mount point: Traverse across remote.
            // Devices are different, even though they also have remotes.  Ignore them.
            *pathout = ".";
            r = vn->WaitForRemote();
            vn->RefRelease();
            return r;
        }

#ifdef __Fuchsia__
        flags |= (must_be_dir ? O_DIRECTORY : 0);
#endif
        r = vn->Open(flags);
        // Open and lookup both incremented the refcount. Release it once for
        // opening a vnode.
        vn->RefRelease();
        if (r < 0) {
            return r;
        }
        if (flags & O_TRUNC) {
            if ((r = vn->Truncate(0)) < 0) {
                if (r != ERR_NOT_SUPPORTED) {
                    // devfs does not support this, but we should not fail
                    vn->RefRelease();
                    return r;
                }
            }
        }
    }
    trace(VFS, "VfsOpen: vn=%p\n", vn);
    *pathout = "";
    *out = vn;
    return NO_ERROR;
}

mx_status_t Vfs::Unlink(Vnode* vndir, const char* path, size_t len) {
    bool must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != NO_ERROR) {
        return r;
    }
    return vndir->Unlink(path, len, must_be_dir);
}

mx_status_t Vfs::Link(Vnode* vndir, const char* oldpath, const char* newpath,
                     const char** oldpathout, const char** newpathout) {
    Vnode *oldparent, *newparent;
    mx_status_t r = 0, r_old, r_new;

    if ((r_old = Vfs::Walk(vndir, &oldparent, oldpath, &oldpath)) < 0) {
        return r_old;
    }
    if ((r_new = Vfs::Walk(vndir, &newparent, newpath, &newpath)) < 0) {
        oldparent->RefRelease();
        return r_new;
    }

    if (r_old != r_new) {
        // Link can only be directed to one filesystem
        r = ERR_NOT_SUPPORTED;
        goto done;
    }

    if (r_old == 0) {
        // Local filesystem
        size_t oldlen = strlen(oldpath);
        size_t newlen = strlen(newpath);
        bool old_must_be_dir;
        bool new_must_be_dir;
        if ((r = vfs_name_trim(oldpath, oldlen, &oldlen, &old_must_be_dir)) != NO_ERROR) {
            goto done;
        } else if (old_must_be_dir) {
            r = ERR_NOT_DIR;
            goto done;
        }

        if ((r = vfs_name_trim(newpath, newlen, &newlen, &new_must_be_dir)) != NO_ERROR) {
            goto done;
        } else if (new_must_be_dir) {
            r = ERR_NOT_DIR;
            goto done;
        }

        // Look up the target vnode
        Vnode* target;
        if ((r = oldparent->Lookup(&target, oldpath, oldlen)) < 0) { // target: +1
            goto done;
        }
        r = newparent->Link(newpath, newlen, target);
        target->RefRelease(); // target: +0
    } else {
        // Remote filesystem -- forward the request
        *oldpathout = oldpath;
        *newpathout = newpath;
        r = r_old;
    }

done:
    oldparent->RefRelease();
    newparent->RefRelease();
    return r;
}

mx_status_t Vfs::Rename(Vnode* vndir, const char* oldpath, const char* newpath,
                             const char** oldpathout, const char** newpathout) {
    Vnode *oldparent, *newparent;
    mx_status_t r = 0, r_old, r_new;

    if ((r_old = Vfs::Walk(vndir, &oldparent, oldpath, &oldpath)) < 0) {
        return r_old;
    }
    if ((r_new = Vfs::Walk(vndir, &newparent, newpath, &newpath)) < 0) {
        oldparent->RefRelease();
        return r_new;
    }

    if (r_old != r_new) {
        // Rename can only be directed to one filesystem
        r = ERR_NOT_SUPPORTED;
        goto done;
    }

    if (r_old == 0) {
        // Local filesystem
        size_t oldlen = strlen(oldpath);
        size_t newlen = strlen(newpath);
        bool old_must_be_dir;
        bool new_must_be_dir;
        if ((r = vfs_name_trim(oldpath, oldlen, &oldlen, &old_must_be_dir)) != NO_ERROR) {
            goto done;
        }
        if ((r = vfs_name_trim(newpath, newlen, &newlen, &new_must_be_dir)) != NO_ERROR) {
            goto done;
        }
        r = oldparent->Rename(newparent, oldpath, oldlen, newpath, newlen,
                              old_must_be_dir, new_must_be_dir);
    } else {
        // Remote filesystem -- forward the request
        *oldpathout = oldpath;
        *newpathout = newpath;
        r = r_old;
    }

done:
    oldparent->RefRelease();
    newparent->RefRelease();
    return r;
}

ssize_t Vfs::Ioctl(Vnode* vn, uint32_t op, const void* in_buf, size_t in_len,
                   void* out_buf, size_t out_len) {
    switch (op) {
#ifdef __Fuchsia__
    case IOCTL_DEVICE_WATCH_DIR: {
        return vn->IoctlWatchDir(in_buf, in_len, out_buf, out_len);
    }
    case IOCTL_DEVMGR_MOUNT_FS: {
        if ((in_len != sizeof(mx_handle_t)) || (out_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h = *(mx_handle_t*)in_buf;
        mx_status_t status;
        if ((status = Vfs::InstallRemote(vn, h)) < 0) {
            // If we can't install the filesystem, we shoot off a quick "unmount"
            // signal to the filesystem process, since we are the owner of its
            // root handle.
            // TODO(smklein): Transfer the mountpoint back to the caller on error,
            // so they can decide what to do with it.
            vfs_unmount_handle(h, 0);
            mx_handle_close(h);
            return status;
        }
        return NO_ERROR;
    }
    case IOCTL_DEVMGR_UNMOUNT_NODE: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t* h = (mx_handle_t*)out_buf;
        return Vfs::UninstallRemote(vn, h);
    }
    case IOCTL_DEVMGR_UNMOUNT_FS: {
        vfs_uninstall_all(MX_TIME_INFINITE);
        vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
        exit(0);
    }
#endif
    default:
        return vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
    }
}

mx_status_t Vfs::Close(Vnode* vn) {
    trace(VFS, "vfs_close: vn=%p\n", vn);
    mx_status_t r = vn->Close();
    return r;
}

void Vnode::RefAcquire() {
    trace(REFS, "acquire vn=%p ref=%u\n", this, refcount_);
    refcount_++;
}

// TODO(orr): figure out x-system panic
#define panic(fmt...)         \
    do {                      \
        fprintf(stderr, fmt); \
        __builtin_trap();     \
    } while (0)

void Vnode::RefRelease() {
    trace(REFS, "release vn=%p ref=%u\n", this, refcount_);
    if (refcount_ == 0) {
        panic("vn %p: ref underflow\n", this);
    }
    refcount_--;
    if (refcount_ == 0) {
        assert(!IsRemote());
        trace(VFS, "vfs_release: vn=%p\n", this);
        Release();
    }
}

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ERR_INVALID_ARGS;
    de->size = static_cast<uint32_t>(sz);
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return static_cast<mx_status_t>(sz);
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
//
// If a non-negative status is returned, the vnode at 'out' has been acquired.
// Otherwise, no net deltas in acquires/releases occur.
mx_status_t Vfs::Walk(Vnode* vn, Vnode** out, const char* path, const char** pathout) {
    Vnode* oldvn = nullptr;
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
            if (oldvn == nullptr) {
                // returning our original vnode, need to upref it
                vn->RefAcquire();
            }
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
            if (oldvn) {
                // release the old vnode, even if there was an error
                oldvn->RefRelease();
            }
            if (r < 0) {
                return r;
            }
            oldvn = vn;
            path = nextpath;
        } else {
            // final path segment, we're done here
            if (oldvn == nullptr) {
                // returning our original vnode, need to upref it
                vn->RefAcquire();
            }
            *out = vn;
            *pathout = path;
            return NO_ERROR;
        }
    }
}

} // namespace fs
