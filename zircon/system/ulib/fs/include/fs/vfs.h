// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <lib/fdio/vfs.h>
#include <fs/locking.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <lib/async/dispatcher.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fs/client.h>
#include <fs/mount_channel.h>
#include <fs/vnode.h>
#endif // __Fuchsia__

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>

#include <utility>

namespace fs {

class Connection;
class Vnode;

inline constexpr bool IsVnodeRefOnly(uint32_t flags) {
    return flags & ZX_FS_FLAG_VNODE_REF_ONLY;
}

inline constexpr bool IsWritable(uint32_t flags) {
    return flags & ZX_FS_RIGHT_WRITABLE;
}

inline constexpr bool IsReadable(uint32_t flags) {
    return flags & ZX_FS_RIGHT_READABLE;
}

inline constexpr bool HasAdminRight(uint32_t flags) {
    return flags & ZX_FS_RIGHT_ADMIN;
}

// A storage class for a vdircookie which is passed to Readdir.
// Common vnode implementations may use this struct as scratch
// space, or cast it to an alternative structure of the same
// size (or smaller).
//
// TODO(smklein): To implement seekdir and telldir, the size
// of this vdircookie may need to shrink to a 'long'.
typedef struct vdircookie {
    void Reset() {
        memset(this, 0, sizeof(struct vdircookie));
    }

    uint64_t n;
    void* p;
} vdircookie_t;

// The Vfs object contains global per-filesystem state, which
// may be valid across a collection of Vnodes.
//
// The Vfs object must outlive the Vnodes which it serves.
//
// This class is thread-safe.
class Vfs {
public:
    Vfs();
    virtual ~Vfs();

    // Traverse the path to the target vnode, and create / open it using
    // the underlying filesystem functions (lookup, create, open).
    //
    // If the node represented by |path| contains a remote node,
    // set |pathout| to the remaining portion of the path yet to
    // be traversed (or ".", if the endpoint of |path| is the mount point),
    // and return the node containing the node in |out|.
    zx_status_t Open(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     fbl::StringPiece path, fbl::StringPiece* pathout,
                     uint32_t flags, uint32_t mode) FS_TA_EXCLUDES(vfs_lock_);
    zx_status_t Unlink(fbl::RefPtr<Vnode> vn, fbl::StringPiece path) FS_TA_EXCLUDES(vfs_lock_);

    // Sets whether this file system is read-only.
    void SetReadonly(bool value) FS_TA_EXCLUDES(vfs_lock_);

#ifdef __Fuchsia__
    // Unmounts the underlying filesystem.
    //
    // The closure may be invoked before or after |Shutdown| returns.
    using ShutdownCallback = fbl::Function<void(zx_status_t status)>;
    virtual void Shutdown(ShutdownCallback closure) = 0;

    // Identifies if the filesystem is in the process of terminating.
    // May be checked by active connections, which, upon reading new
    // port packets, should ignore them and close immediately.
    virtual bool IsTerminating() const = 0;

    void TokenDiscard(zx::event ios_token) FS_TA_EXCLUDES(vfs_lock_);
    zx_status_t VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token,
                             zx::event* out) FS_TA_EXCLUDES(vfs_lock_);
    zx_status_t Link(zx::event token, fbl::RefPtr<Vnode> oldparent,
                     fbl::StringPiece oldStr, fbl::StringPiece newStr) FS_TA_EXCLUDES(vfs_lock_);
    zx_status_t Rename(zx::event token, fbl::RefPtr<Vnode> oldparent,
                       fbl::StringPiece oldStr, fbl::StringPiece newStr) FS_TA_EXCLUDES(vfs_lock_);
    // Calls readdir on the Vnode while holding the vfs_lock, preventing path
    // modification operations for the duration of the operation.
    zx_status_t Readdir(Vnode* vn, vdircookie_t* cookie,
                        void* dirents, size_t len, size_t* out_actual) FS_TA_EXCLUDES(vfs_lock_);

    Vfs(async_dispatcher_t* dispatcher);

    async_dispatcher_t* dispatcher() { return dispatcher_; }
    void SetDispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }

    // Begins serving VFS messages over the specified connection.
    zx_status_t ServeConnection(fbl::unique_ptr<Connection> connection) FS_TA_EXCLUDES(vfs_lock_);

    // Called by a VFS connection when it is closed remotely.
    // The VFS is now responsible for destroying the connection.
    void OnConnectionClosedRemotely(Connection* connection) FS_TA_EXCLUDES(vfs_lock_);

    // Serves a Vnode over the specified channel (used for creating new filesystems)
    zx_status_t ServeDirectory(fbl::RefPtr<Vnode> vn, zx::channel channel, uint32_t rights);

    // Convenience wrapper over |ServeDirectory| with maximum default permissions.
    zx_status_t ServeDirectory(fbl::RefPtr<Vnode> vn, zx::channel channel) {
        return ServeDirectory(vn, std::move(channel), ZX_FS_RIGHTS);
    }

    // Pins a handle to a remote filesystem onto a vnode, if possible.
    zx_status_t InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) FS_TA_EXCLUDES(vfs_lock_);

    // Create and mount a directory with a provided name
    zx_status_t MountMkdir(fbl::RefPtr<Vnode> vn, fbl::StringPiece name,
                           MountChannel h, uint32_t flags) FS_TA_EXCLUDES(vfs_lock_);

    // Unpin a handle to a remote filesystem from a vnode, if one exists.
    zx_status_t UninstallRemote(fbl::RefPtr<Vnode> vn, zx::channel* h) FS_TA_EXCLUDES(vfs_lock_);

    // Forwards an open request to a remote handle.
    // If the remote handle is closed (handing off returns ZX_ERR_PEER_CLOSED),
    // it is automatically unmounted.
    zx_status_t ForwardOpenRemote(fbl::RefPtr<Vnode> vn, zx::channel channel,
                                  fbl::StringPiece path, uint32_t flags,
                                  uint32_t mode) FS_TA_EXCLUDES(vfs_lock_);

    // Unpins all remote filesystems in the current filesystem, and waits for the
    // response of each one with the provided deadline.
    zx_status_t UninstallAll(zx_time_t deadline) FS_TA_EXCLUDES(vfs_lock_);
#endif

protected:
    // Whether this file system is read-only.
    bool ReadonlyLocked() const FS_TA_REQUIRES(vfs_lock_) { return readonly_; }

private:
    // Starting at vnode |vn|, walk the tree described by the path string,
    // until either there is only one path segment remaining in the string
    // or we encounter a vnode that represents a remote filesystem
    //
    // On success,
    // |out| is the vnode at which we stopped searching.
    // |pathout| is the remainder of the path to search.
    zx_status_t Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     fbl::StringPiece path, fbl::StringPiece* pathout) FS_TA_REQUIRES(vfs_lock_);

    zx_status_t OpenLocked(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                           fbl::StringPiece path, fbl::StringPiece* pathout,
                           uint32_t flags, uint32_t mode) FS_TA_REQUIRES(vfs_lock_);

    bool readonly_{};

#ifdef __Fuchsia__
    zx_status_t TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) FS_TA_REQUIRES(vfs_lock_);
    zx_status_t InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) FS_TA_REQUIRES(vfs_lock_);
    zx_status_t UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                      zx::channel* h) FS_TA_REQUIRES(vfs_lock_);

    fbl::HashTable<zx_koid_t, std::unique_ptr<VnodeToken>> vnode_tokens_;

    // Non-intrusive node in linked list of vnodes acting as mount points
    class MountNode final : public fbl::DoublyLinkedListable<fbl::unique_ptr<MountNode>> {
    public:
        using ListType = fbl::DoublyLinkedList<fbl::unique_ptr<MountNode>>;
        constexpr MountNode();
        ~MountNode();

        void SetNode(fbl::RefPtr<Vnode> vn);
        zx::channel ReleaseRemote();
        bool VnodeMatch(fbl::RefPtr<Vnode> vn) const;

    private:
        fbl::RefPtr<Vnode> vn_;
    };

    // The mount list is a global static variable, but it only uses
    // constexpr constructors during initialization. As a consequence,
    // the .init_array section of the compiled vfs-mount object file is
    // empty; "remote_list" is a member of the bss section.
    MountNode::ListType remote_list_ FS_TA_GUARDED(vfs_lock_){};

    async_dispatcher_t* dispatcher_{};

protected:
    // A lock which should be used to protect lookup and walk operations
    mtx_t vfs_lock_{};

    // Starts tracking the lifetime of the connection.
    virtual void RegisterConnection(fbl::unique_ptr<Connection> connection) = 0;

    // Stops tracking the lifetime of the connection.
    virtual void UnregisterConnection(Connection* connection) = 0;

#endif // ifdef __Fuchsia__
};

} // namespace fs
