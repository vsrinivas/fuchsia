// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/fdio/watcher.h>

#ifdef __Fuchsia__
#include <threads.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fs/connection.h>
#include <fs/remote.h>
#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>

#include <utility>
#endif

#include "debug.h"

namespace fs {
namespace {

// Trim a name before sending it to internal filesystem functions.
// Trailing '/' characters imply that the name must refer to a directory.
zx_status_t TrimName(fbl::StringPiece name, fbl::StringPiece* name_out, bool* dir_out) {
    size_t len = name.length();
    bool is_dir = false;
    while ((len > 0) && name[len - 1] == '/') {
        len--;
        is_dir = true;
    }

    if (len == 0) {
        // 'name' should not contain paths consisting of exclusively '/' characters.
        return ZX_ERR_INVALID_ARGS;
    } else if (len > NAME_MAX) {
        // Name must be less than the maximum-expected length.
        return ZX_ERR_BAD_PATH;
    } else if (memchr(name.data(), '/', len) != nullptr) {
        // Name must not contain '/' characters after being trimmed.
        return ZX_ERR_INVALID_ARGS;
    }

    name_out->set(name.data(), len);
    *dir_out = is_dir;
    return ZX_OK;
}

zx_status_t LookupNode(fbl::RefPtr<Vnode> vn, fbl::StringPiece name, fbl::RefPtr<Vnode>* out) {
    if (name == "..") {
        return ZX_ERR_INVALID_ARGS;
    } else if (name == ".") {
        *out = std::move(vn);
        return ZX_OK;
    }
    return vn->Lookup(out, name);
}

// Validate open flags as much as they can be validated
// independently of the target node.
zx_status_t PrevalidateFlags(uint32_t flags) {
    if (!(flags & ZX_FS_RIGHT_WRITABLE)) {
        if (flags & ZX_FS_FLAG_TRUNCATE) {
            return ZX_ERR_INVALID_ARGS;
        }
    } else if (!(flags & ZX_FS_RIGHTS)) {
        if (!IsVnodeRefOnly(flags)) {
            return ZX_ERR_INVALID_ARGS;
        }
    }
    return ZX_OK;
}

} // namespace

#ifdef __Fuchsia__

bool RemoteContainer::IsRemote() const {
    return remote_.is_valid();
}

zx::channel RemoteContainer::DetachRemote() {
    return std::move(remote_);
}

zx_handle_t RemoteContainer::GetRemote() const {
    return remote_.get();
}

void RemoteContainer::SetRemote(zx::channel remote) {
    ZX_DEBUG_ASSERT(!remote_.is_valid());
    remote_ = std::move(remote);
}

#endif

Vfs::Vfs() = default;
Vfs::~Vfs() = default;

#ifdef __Fuchsia__
Vfs::Vfs(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}
#endif

zx_status_t Vfs::Open(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                      fbl::StringPiece path, fbl::StringPiece* out_path, uint32_t flags,
                      uint32_t mode) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    return OpenLocked(std::move(vndir), out, path, out_path, flags, mode);
}

zx_status_t Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, fbl::RefPtr<Vnode>* out,
                            fbl::StringPiece path, fbl::StringPiece* out_path,
                            uint32_t flags, uint32_t mode) {
    FS_PRETTY_TRACE_DEBUG("VfsOpen: path='", Path(path.data(), path.size()),
                          "' flags=", ZxFlags(flags));
    zx_status_t r;
    if ((r = PrevalidateFlags(flags)) != ZX_OK) {
        return r;
    }
    if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
#ifdef __Fuchsia__
    if (vndir->IsRemote()) {
        // remote filesystem, return handle and path through to caller
        *out = std::move(vndir);
        *out_path = path;
        return ZX_OK;
    }
#endif

    fbl::RefPtr<Vnode> vn;

    bool must_be_dir = false;
    if ((r = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
        return r;
    } else if (path == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if (flags & ZX_FS_FLAG_CREATE) {
        if (must_be_dir && !S_ISDIR(mode)) {
            return ZX_ERR_INVALID_ARGS;
        } else if (path == ".") {
            return ZX_ERR_INVALID_ARGS;
        } else if (ReadonlyLocked()) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = vndir->Create(&vn, path, mode)) < 0) {
            if ((r == ZX_ERR_ALREADY_EXISTS) && (!(flags & ZX_FS_FLAG_EXCLUSIVE))) {
                goto try_open;
            }
            if (r == ZX_ERR_NOT_SUPPORTED) {
                // filesystem may not support create (like devfs)
                // in which case we should still try to open() the file
                goto try_open;
            }
            return r;
        }
#ifdef __Fuchsia__
        vndir->Notify(path, fuchsia_io_WATCH_EVENT_ADDED);
#endif
    } else {
    try_open:
        r = LookupNode(std::move(vndir), path, &vn);
        if (r < 0) {
            return r;
        }
#ifdef __Fuchsia__
        if (!(flags & ZX_FS_FLAG_NOREMOTE) && vn->IsRemote()) {
            // Opening a mount point: Traverse across remote.
            *out_path = ".";
            *out = std::move(vn);
            return ZX_OK;
        }

        if (must_be_dir) {
            flags |= ZX_FS_FLAG_DIRECTORY;
        }
#endif
        if (ReadonlyLocked() && IsWritable(flags)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = vn->ValidateFlags(flags)) != ZX_OK) {
            return r;
        }
        // VNODE_REF_ONLY requests that we don't actually open the underlying Vnode,
        // but use the connection as a reference to the Vnode.
        if (!IsVnodeRefOnly(flags)) {
            if ((r = OpenVnode(flags, &vn)) != ZX_OK) {
                return r;
            }
            if ((flags & ZX_FS_FLAG_TRUNCATE) && ((r = vn->Truncate(0)) < 0)) {
                vn->Close();
                return r;
            }
        }
    }
    FS_TRACE_DEBUG("VfsOpen: vn=%p\n", vn.get());
    *out_path = "";
    *out = vn;
    return ZX_OK;
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path) {
    bool must_be_dir;
    zx_status_t r;
    if ((r = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
        return r;
    } else if (path == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (path == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    {
#ifdef __Fuchsia__
        fbl::AutoLock lock(&vfs_lock_);
#endif
        if (ReadonlyLocked()) {
            r = ZX_ERR_ACCESS_DENIED;
        } else {
            r = vndir->Unlink(path, must_be_dir);
        }
    }
    if (r != ZX_OK) {
        return r;
    }
#ifdef __Fuchsia__
    vndir->Notify(path, fuchsia_io_WATCH_EVENT_REMOVED);
#endif
    return ZX_OK;
}

#ifdef __Fuchsia__

#define TOKEN_RIGHTS (ZX_RIGHTS_BASIC)

namespace {
zx_koid_t GetTokenKoid(const zx::event& token) {
    zx_info_handle_basic_t info = {};
    token.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    return info.koid;
}
} // namespace

void Vfs::TokenDiscard(zx::event ios_token) {
    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token) {
        // The token is cleared here to prevent the following race condition:
        // 1) Open
        // 2) GetToken
        // 3) Close + Release Vnode
        // 4) Use token handle to access defunct vnode (or a different vnode,
        //    if the memory for it is reallocated).
        //
        // By cleared the token cookie, any remaining handles to the event will
        // be ignored by the filesystem server.
        auto rename_request = vnode_tokens_.erase(GetTokenKoid(ios_token));
    }
}

zx_status_t Vfs::VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token,
                              zx::event* out) {
    zx_status_t r;

    fbl::AutoLock lock(&vfs_lock_);
    if (ios_token->is_valid()) {
        // Token has already been set for this iostate
        if ((r = ios_token->duplicate(TOKEN_RIGHTS, out) != ZX_OK)) {
            return r;
        }
        return ZX_OK;
    }

    zx::event new_token;
    zx::event new_ios_token;
    if ((r = zx::event::create(0, &new_ios_token)) != ZX_OK) {
        return r;
    } else if ((r = new_ios_token.duplicate(TOKEN_RIGHTS, &new_token) != ZX_OK)) {
        return r;
    }
    auto koid = GetTokenKoid(new_ios_token);
    vnode_tokens_.insert(std::make_unique<VnodeToken>(koid, std::move(vn)));
    *ios_token = std::move(new_ios_token);
    *out = std::move(new_token);
    return ZX_OK;
}

zx_status_t Vfs::TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) {
    const auto& vnode_token = vnode_tokens_.find(GetTokenKoid(token));
    if (vnode_token == vnode_tokens_.end()) {
        // TODO(smklein): Return a more specific error code for "token not from this server"
        return ZX_ERR_INVALID_ARGS;
    }

    *out = vnode_token->get_vnode();
    return ZX_OK;
}

zx_status_t Vfs::Rename(zx::event token, fbl::RefPtr<Vnode> oldparent,
                        fbl::StringPiece oldStr, fbl::StringPiece newStr) {
    // Local filesystem
    bool old_must_be_dir;
    bool new_must_be_dir;
    zx_status_t r;
    if ((r = TrimName(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
        return r;
    } else if (oldStr == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (oldStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if ((r = TrimName(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
        return r;
    } else if (newStr == "." || newStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<fs::Vnode> newparent;
    {
        fbl::AutoLock lock(&vfs_lock_);
        if (ReadonlyLocked()) {
            return ZX_ERR_ACCESS_DENIED;
        }
        if ((r = TokenToVnode(std::move(token), &newparent)) != ZX_OK) {
            return r;
        }

        r = oldparent->Rename(newparent, oldStr, newStr, old_must_be_dir,
                              new_must_be_dir);
    }
    if (r != ZX_OK) {
        return r;
    }
    oldparent->Notify(oldStr, fuchsia_io_WATCH_EVENT_REMOVED);
    newparent->Notify(newStr, fuchsia_io_WATCH_EVENT_ADDED);
    return ZX_OK;
}

zx_status_t Vfs::Readdir(Vnode* vn, vdircookie_t* cookie,
                         void* dirents, size_t len, size_t* out_actual) {
    fbl::AutoLock lock(&vfs_lock_);
    return vn->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Vfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent,
                      fbl::StringPiece oldStr, fbl::StringPiece newStr) {
    fbl::AutoLock lock(&vfs_lock_);
    fbl::RefPtr<fs::Vnode> newparent;
    zx_status_t r;
    if ((r = TokenToVnode(std::move(token), &newparent)) != ZX_OK) {
        return r;
    }
    // Local filesystem
    bool old_must_be_dir;
    bool new_must_be_dir;
    if (ReadonlyLocked()) {
        return ZX_ERR_ACCESS_DENIED;
    } else if ((r = TrimName(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
        return r;
    } else if (old_must_be_dir) {
        return ZX_ERR_NOT_DIR;
    } else if (oldStr == ".") {
        return ZX_ERR_UNAVAILABLE;
    } else if (oldStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    if ((r = TrimName(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
        return r;
    } else if (new_must_be_dir) {
        return ZX_ERR_NOT_DIR;
    } else if (newStr == "." || newStr == "..") {
        return ZX_ERR_INVALID_ARGS;
    }

    // Look up the target vnode
    fbl::RefPtr<Vnode> target;
    if ((r = oldparent->Lookup(&target, oldStr)) < 0) {
        return r;
    }
    r = newparent->Link(newStr, target);
    if (r != ZX_OK) {
        return r;
    }
    newparent->Notify(newStr, fuchsia_io_WATCH_EVENT_ADDED);
    return ZX_OK;
}

zx_status_t Vfs::ServeConnection(fbl::unique_ptr<Connection> connection) {
    ZX_DEBUG_ASSERT(connection);

    zx_status_t status = connection->Serve();
    if (status == ZX_OK) {
        RegisterConnection(std::move(connection));
    }
    return status;
}

void Vfs::OnConnectionClosedRemotely(Connection* connection) {
    ZX_DEBUG_ASSERT(connection);

    UnregisterConnection(connection);
}

zx_status_t Vfs::ServeDirectory(fbl::RefPtr<fs::Vnode> vn, zx::channel channel, uint32_t rights) {
    const uint32_t flags = ZX_FS_FLAG_DIRECTORY;
    zx_status_t r;
    if ((r = vn->ValidateFlags(flags)) != ZX_OK) {
        return r;
    } else if ((r = OpenVnode(flags, &vn)) != ZX_OK) {
        return r;
    }

    // Tell the calling process that we've mounted the directory.
    r = channel.signal_peer(0, ZX_USER_SIGNAL_0);
    // ZX_ERR_PEER_CLOSED is ok because the channel may still be readable.
    if (r != ZX_OK && r != ZX_ERR_PEER_CLOSED) {
        return r;
    }

    return vn->Serve(this, std::move(channel), flags | rights);
}

#endif // ifdef __Fuchsia__

void Vfs::SetReadonly(bool value) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    readonly_ = value;
}

zx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out_vn,
                      fbl::StringPiece path, fbl::StringPiece* out_path) {
    zx_status_t r;
    while (!path.empty() && path[path.length() - 1] == '/') {
        // Discard extra trailing '/' characters.
        path.set(path.data(), path.length() - 1);
    }

    for (;;) {
        while (!path.empty() && path[0] == '/') {
            // Discard extra leading '/' characters.
            path.set(&path[1], path.length() - 1);
        }
        if (path.empty()) {
            // Convert empty initial path of final path segment to ".".
            path.set(".", 1);
        }
#ifdef __Fuchsia__
        if (vn->IsRemote()) {
            // Remote filesystem mount, caller must resolve.
            *out_vn = std::move(vn);
            *out_path = std::move(path);
            return ZX_OK;
        }
#endif

        // Look for the next '/' separated path component.
        const char* next_path = reinterpret_cast<const char*>(
                memchr(path.data(), '/', path.length()));
        if (next_path == nullptr) {
            // Final path segment.
            *out_vn = vn;
            *out_path = std::move(path);
            return ZX_OK;
        }

        // Path has at least one additional segment.
        fbl::StringPiece component(path.data(), next_path - path.data());
        if ((r = LookupNode(std::move(vn), component, &vn)) != ZX_OK) {
            return r;
        }
        // Traverse to the next segment.
        path.set(next_path + 1, path.length() - (component.length() + 1));
    }
}

} // namespace fs
