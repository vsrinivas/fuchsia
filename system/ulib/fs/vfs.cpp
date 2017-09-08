// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mxio/remoteio.h>
#include <mxio/watcher.h>
#include <fbl/auto_call.h>

#ifdef __Fuchsia__
#include <threads.h>
#include <magenta/assert.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mx/event.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fs/remote.h>
#endif

#include <fs/vfs.h>

uint32_t __trace_bits;

namespace fs {
namespace {

bool is_dot(const char* name, size_t len) {
    return len == 1 && strncmp(name, ".", len) == 0;
}

bool is_dot_dot(const char* name, size_t len) {
    return len == 2 && strncmp(name, "..", len) == 0;
}

#ifdef __Fuchsia__  // Only to prevent "unused function" warning
bool is_dot_or_dot_dot(const char* name, size_t len) {
    return is_dot(name, len) || is_dot_dot(name, len);
}
#endif

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
        return MX_ERR_INVALID_ARGS;
    } else if (len > NAME_MAX) {
        return MX_ERR_BAD_PATH;
    }

    *len_out = len;
    *dir_out = is_dir;
    return MX_OK;
}

mx_status_t vfs_lookup(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                       const char* name, size_t len) {
    if (is_dot_dot(name, len)) {
        return MX_ERR_INVALID_ARGS;
    } else if (is_dot(name, len)) {
        *out = fbl::move(vn);
        return MX_OK;
    }
    return vn->Lookup(out, name, len);
}

// Validate open flags as much as they can be validated
// independently of the target node.
mx_status_t vfs_validate_flags(uint32_t flags) {
    switch (flags & 3) {
    case O_RDONLY:
        if (flags & O_TRUNC) {
            return MX_ERR_INVALID_ARGS;
        }
    case O_WRONLY:
    case O_RDWR:
        return MX_OK;
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

} // namespace anonymous

#ifdef __Fuchsia__

bool RemoteContainer::IsRemote() const {
    return remote_.is_valid();
}

mx::channel RemoteContainer::DetachRemote(uint32_t &flags_) {
    flags_ &= ~VFS_FLAG_MOUNT_READY;
    return fbl::move(remote_);
}

// Access the remote handle if it's ready -- otherwise, return an error.
mx_handle_t RemoteContainer::WaitForRemote(uint32_t &flags_) {
    if (!remote_.is_valid()) {
        // Trying to get remote on a non-remote vnode
        return MX_ERR_UNAVAILABLE;
    } else if (!(flags_ & VFS_FLAG_MOUNT_READY)) {
        mx_signals_t observed;
        mx_status_t status = remote_.wait_one(MX_USER_SIGNAL_0 | MX_CHANNEL_PEER_CLOSED,
                                              0,
                                              &observed);
        // Not set (or otherwise remote is bad)
        // TODO(planders): Add a background thread that waits on all remotes
        if (observed & MX_CHANNEL_PEER_CLOSED) {
            return MX_ERR_PEER_CLOSED;
        } else if ((status != MX_OK)) {
            return MX_ERR_UNAVAILABLE;
        }

        flags_ |= VFS_FLAG_MOUNT_READY;
    }
    return remote_.get();
}

mx_handle_t RemoteContainer::GetRemote() const {
    return remote_.get();
}

void RemoteContainer::SetRemote(mx::channel remote) {
    MX_DEBUG_ASSERT(!remote_.is_valid());
    remote_ = fbl::move(remote);
}

#endif

Vfs::Vfs() = default;

#ifdef __Fuchsia__
Vfs::Vfs(Dispatcher* dispatcher) : dispatcher_(dispatcher) {}
#endif

mx_status_t Vfs::Open(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                      const char* path, const char** pathout, uint32_t flags,
                      uint32_t mode) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    return OpenLocked(fbl::move(vndir), out, path, pathout, flags, mode);
}

mx_status_t Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                            const char* path, const char** pathout, uint32_t flags,
                            uint32_t mode) {
    FS_TRACE(VFS, "VfsOpen: path='%s' flags=%d\n", path, flags);
    mx_status_t r;
    if ((r = vfs_validate_flags(flags)) != MX_OK) {
        return r;
    }
    if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
    if (r > 0) {
        // remote filesystem, return handle and path through to caller
        *pathout = path;
        return r;
    }

    size_t len = strlen(path);
    fbl::RefPtr<Vnode> vn;

    bool must_be_dir = false;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != MX_OK) {
        return r;
    } else if (is_dot_dot(path, len)) {
        return MX_ERR_INVALID_ARGS;
    }

    if (flags & O_CREAT) {
        if (must_be_dir && !S_ISDIR(mode)) {
            return MX_ERR_INVALID_ARGS;
        } else if (is_dot(path, len)) {
            return MX_ERR_INVALID_ARGS;
        }

        if ((r = vndir->Create(&vn, path, len, mode)) < 0) {
            if ((r == MX_ERR_ALREADY_EXISTS) && (!(flags & O_EXCL))) {
                goto try_open;
            }
            if (r == MX_ERR_NOT_SUPPORTED) {
                // filesystem may not support create (like devfs)
                // in which case we should still try to open() the file
                goto try_open;
            }
            return r;
        }
        vndir->Notify(path, len, VFS_WATCH_EVT_ADDED);
    } else {
    try_open:
        r = vfs_lookup(fbl::move(vndir), &vn, path, len);
        if (r < 0) {
            return r;
        }
#ifdef __Fuchsia__
        if (!(flags & O_NOREMOTE) && vn->IsRemote() && !vn->IsDevice()) {
            // Opening a mount point: Traverse across remote.
            // Devices are different, even though they also have remotes.  Ignore them.
            *pathout = ".";

            if ((r = Vfs::WaitForRemoteLocked(vn)) != MX_ERR_PEER_CLOSED) {
                return r;
            }
        }

        flags |= (must_be_dir ? O_DIRECTORY : 0);
#endif
        if ((r = vn->Open(flags)) < 0) {
            return r;
        }
#ifdef __Fuchsia__
        if (vn->IsDevice() && !(flags & O_DIRECTORY)) {
            *pathout = ".";
            r = vn->GetRemote();
            return r;
        }
#endif
        if ((flags & O_TRUNC) && ((r = vn->Truncate(0)) < 0)) {
            return r;
        }
    }
    FS_TRACE(VFS, "VfsOpen: vn=%p\n", vn.get());
    *pathout = "";
    *out = vn;
    return MX_OK;
}

mx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, const char* path, size_t len) {
    bool must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != MX_OK) {
        return r;
    } else if (is_dot(path, len)) {
        return MX_ERR_UNAVAILABLE;
    } else if (is_dot_dot(path, len)) {
        return MX_ERR_INVALID_ARGS;
    }

    {
#ifdef __Fuchsia__
        fbl::AutoLock lock(&vfs_lock_);
#endif
        r = vndir->Unlink(path, len, must_be_dir);
    }
    if (r != MX_OK) {
        return r;
    }
    vndir->Notify(path, len, VFS_WATCH_EVT_REMOVED);
    return MX_OK;
}

#ifdef __Fuchsia__

#define TOKEN_RIGHTS (MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER)

void Vfs::TokenDiscard(mx::event* ios_token) {
    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token->is_valid()) {
        // The token is nullified here to prevent the following race condition:
        // 1) Open
        // 2) GetToken
        // 3) Close + Release Vnode
        // 4) Use token handle to access defunct vnode (or a different vnode,
        //    if the memory for it is reallocated).
        //
        // By nullifying the token cookie, any remaining handles to the event will
        // be ignored by the filesystem server.
        ios_token->set_cookie(mx_process_self(), 0);
        ios_token->reset();
    }
}

mx_status_t Vfs::VnodeToToken(fbl::RefPtr<Vnode> vn, mx::event* ios_token,
                              mx::event* out) {
    uint64_t vnode_cookie = reinterpret_cast<uint64_t>(vn.get());
    mx_status_t r;

    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token->is_valid()) {
        // Token has already been set for this iostate
        if ((r = ios_token->duplicate(TOKEN_RIGHTS, out) != MX_OK)) {
            return r;
        }
        return MX_OK;
    }

    mx::event new_token;
    mx::event new_ios_token;
    if ((r = mx::event::create(0, &new_ios_token)) != MX_OK) {
        return r;
    } else if ((r = new_ios_token.duplicate(TOKEN_RIGHTS, &new_token) != MX_OK)) {
        return r;
    } else if ((r = new_ios_token.set_cookie(mx_process_self(), vnode_cookie)) != MX_OK) {
        return r;
    }
    *ios_token = fbl::move(new_ios_token);
    *out = fbl::move(new_token);
    return MX_OK;
}

mx_status_t Vfs::TokenToVnode(mx::event token, fbl::RefPtr<Vnode>* out) {
    uint64_t vcookie;
    mx_status_t r;
    if ((r = token.get_cookie(mx_process_self(), &vcookie)) < 0) {
        // TODO(smklein): Return a more specific error code for "token not from this server"
        return MX_ERR_INVALID_ARGS;
    }

    if (vcookie == 0) {
        // Client closed the channel associated with the token
        return MX_ERR_INVALID_ARGS;
    }

    *out = fbl::RefPtr<fs::Vnode>(reinterpret_cast<fs::Vnode*>(vcookie));
    return MX_OK;
}

mx_status_t Vfs::Rename(mx::event token, fbl::RefPtr<Vnode> oldparent,
                        const char* oldname, const char* newname) {
    // Local filesystem
    size_t oldlen = strlen(oldname);
    size_t newlen = strlen(newname);
    bool old_must_be_dir;
    bool new_must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(oldname, oldlen, &oldlen, &old_must_be_dir)) != MX_OK) {
        return r;
    } else if (is_dot(oldname, oldlen)) {
        return MX_ERR_UNAVAILABLE;
    } else if (is_dot_dot(oldname, oldlen)) {
        return MX_ERR_INVALID_ARGS;
    }


    if ((r = vfs_name_trim(newname, newlen, &newlen, &new_must_be_dir)) != MX_OK) {
        return r;
    } else if (is_dot_or_dot_dot(newname, newlen)) {
        return MX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<fs::Vnode> newparent;
    {
        fbl::AutoLock lock(&vfs_lock_);
        if ((r = TokenToVnode(fbl::move(token), &newparent)) != MX_OK) {
            return r;
        }

        r = oldparent->Rename(newparent, oldname, oldlen, newname, newlen,
                              old_must_be_dir, new_must_be_dir);
    }
    if (r != MX_OK) {
        return r;
    }
    oldparent->Notify(oldname, oldlen, VFS_WATCH_EVT_REMOVED);
    newparent->Notify(newname, newlen, VFS_WATCH_EVT_ADDED);
    return MX_OK;
}

mx_status_t Vfs::Link(mx::event token, fbl::RefPtr<Vnode> oldparent,
                      const char* oldname, const char* newname) {
    fbl::AutoLock lock(&vfs_lock_);
    fbl::RefPtr<fs::Vnode> newparent;
    mx_status_t r;
    if ((r = TokenToVnode(fbl::move(token), &newparent)) != MX_OK) {
        return r;
    }
    // Local filesystem
    size_t oldlen = strlen(oldname);
    size_t newlen = strlen(newname);
    bool old_must_be_dir;
    bool new_must_be_dir;
    if ((r = vfs_name_trim(oldname, oldlen, &oldlen, &old_must_be_dir)) != MX_OK) {
        return r;
    } else if (old_must_be_dir) {
        return MX_ERR_NOT_DIR;
    } else if (is_dot(oldname, oldlen)) {
        return MX_ERR_UNAVAILABLE;
    } else if (is_dot_dot(oldname, oldlen)) {
        return MX_ERR_INVALID_ARGS;
    }

    if ((r = vfs_name_trim(newname, newlen, &newlen, &new_must_be_dir)) != MX_OK) {
        return r;
    } else if (new_must_be_dir) {
        return MX_ERR_NOT_DIR;
    } else if (is_dot_or_dot_dot(newname, newlen)) {
        return MX_ERR_INVALID_ARGS;
    }

    // Look up the target vnode
    fbl::RefPtr<Vnode> target;
    if ((r = oldparent->Lookup(&target, oldname, oldlen)) < 0) {
        return r;
    }
    r = newparent->Link(newname, newlen, target);
    if (r != MX_OK) {
        return r;
    }
    newparent->Notify(newname, newlen, VFS_WATCH_EVT_ADDED);
    return MX_OK;
}

mx_handle_t Vfs::WaitForRemoteLocked(fbl::RefPtr<Vnode> vn) {
    mx_handle_t h = vn->WaitForRemote();

    if (h == MX_ERR_PEER_CLOSED) {
        printf("VFS: Remote filesystem channel closed, unmounting\n");
        mx::channel c;
        mx_status_t status;
        if ((status = Vfs::UninstallRemoteLocked(vn, &c)) != MX_OK) {
            return status;
        }
    }

    return h;
}

#endif  // idfdef __Fuchsia__

ssize_t Vfs::Ioctl(fbl::RefPtr<Vnode> vn, uint32_t op, const void* in_buf, size_t in_len,
                   void* out_buf, size_t out_len) {
    switch (op) {
#ifdef __Fuchsia__
    case IOCTL_VFS_WATCH_DIR: {
        if (in_len != sizeof(vfs_watch_dir_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        const vfs_watch_dir_t* request = reinterpret_cast<const vfs_watch_dir_t*>(in_buf);
        return vn->WatchDirV2(this, request);
    }
    case IOCTL_VFS_MOUNT_FS: {
        if ((in_len != sizeof(mx_handle_t)) || (out_len != 0)) {
            return MX_ERR_INVALID_ARGS;
        }
        MountChannel h = MountChannel(*reinterpret_cast<const mx_handle_t*>(in_buf));
        return Vfs::InstallRemote(vn, fbl::move(h));
    }
    case IOCTL_VFS_MOUNT_MKDIR_FS: {
        size_t namelen = in_len - sizeof(mount_mkdir_config_t);
        const mount_mkdir_config_t* config = reinterpret_cast<const mount_mkdir_config_t*>(in_buf);
        const char* name = config->name;
        if ((in_len < sizeof(mount_mkdir_config_t)) ||
            (namelen < 1) || (namelen > PATH_MAX) || (name[namelen - 1] != 0) ||
            (out_len != 0)) {
            return MX_ERR_INVALID_ARGS;
        }

        return Vfs::MountMkdir(fbl::move(vn), config);
    }
    case IOCTL_VFS_UNMOUNT_NODE: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return MX_ERR_INVALID_ARGS;
        }
        mx_handle_t* h = (mx_handle_t*)out_buf;
        mx::channel c;
        mx_status_t s = Vfs::UninstallRemote(vn, &c);
        *h = c.release();
        return s;
    }
    case IOCTL_VFS_UNMOUNT_FS: {
        Vfs::UninstallAll(MX_TIME_INFINITE);
        vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
        return MX_OK;
    }
#endif
    default:
        return vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
    }
}

mx_status_t Vnode::Close() {
    return MX_OK;
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
        return MX_ERR_INVALID_ARGS;
    }
    de->size = static_cast<uint32_t>(sz);
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    pos_ += sz;
    return MX_OK;
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
//
// If a non-negative status is returned, the vnode at 'out' has been acquired.
// Otherwise, no net deltas in acquires/releases occur.
mx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
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
#ifdef __Fuchsia__
        if (vn->IsRemote() && !vn->IsDevice()) {
            // remote filesystem mount, caller must resolve
            // devices are different, so ignore them even though they can have vn->remote
            r = Vfs::WaitForRemoteLocked(vn);
            if (r != MX_ERR_PEER_CLOSED) {
                if (r >= 0) {
                    *out = vn;
                    *pathout = path;
                }
                return r;
            }
        }
#endif

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
            if ((r = vfs_lookup(fbl::move(vn), &vn, path, len)) < 0) {
                return r;
            }
            path = nextpath;
        } else {
            // final path segment, we're done here
            *out = vn;
            *pathout = path;
            return MX_OK;
        }
    }
}

} // namespace fs
