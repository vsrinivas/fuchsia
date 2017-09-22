// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <fdio/remoteio.h>
#include <fdio/vfs.h>
#include <fs/client.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <fdio/io.h>
#include <threads.h>
#endif

// VFS Helpers (vfs.c)
// clang-format off
#define VFS_FLAG_DEVICE          0x00000001
#define VFS_FLAG_MOUNT_READY     0x00000002
#define VFS_FLAG_DEVICE_DETACHED 0x00000004
#define VFS_FLAG_RESERVED_MASK   0x0000FFFF
// clang-format on

#ifdef __Fuchsia__
#include <async/dispatcher.h>
#include <zx/channel.h>
#include <zx/event.h>
#include <zx/vmo.h>
#include <fbl/mutex.h>
#endif // __Fuchsia__

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

namespace fs {

class Connection;
class Vnode;

inline constexpr bool IsWritable(uint32_t flags) {
    uint32_t mode = flags & O_ACCMODE;
    return mode == O_RDWR || mode == O_WRONLY;
}

inline constexpr bool IsReadable(uint32_t flags) {
    uint32_t mode = flags & O_ACCMODE;
    return mode == O_RDWR || mode == O_RDONLY;
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

#ifdef __Fuchsia__

// MountChannel functions exactly the same as a channel, except that it
// intentionally destructs by sending a clean "shutdown" signal to the
// underlying filesystem. Up until the point that a remote handle is
// attached to a vnode, this wrapper guarantees not only that the
// underlying handle gets closed on error, but also that the sub-filesystem
// is released (which cleans up the underlying connection to the block
// device).
class MountChannel {
public:
    constexpr MountChannel() = default;
    explicit MountChannel(zx_handle_t handle)
        : channel_(handle) {}
    explicit MountChannel(zx::channel channel)
        : channel_(fbl::move(channel)) {}
    MountChannel(MountChannel&& other)
        : channel_(fbl::move(other.channel_)) {}

    zx::channel TakeChannel() { return fbl::move(channel_); }

    ~MountChannel() {
        if (channel_.is_valid()) {
            vfs_unmount_handle(channel_.release(), 0);
        }
    }

private:
    zx::channel channel_;
};

#endif // __Fuchsia__

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
    zx_status_t Open(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     const char* path, const char** pathout,
                     uint32_t flags, uint32_t mode) __TA_EXCLUDES(vfs_lock_);
    zx_status_t Unlink(fbl::RefPtr<Vnode> vn, const char* path, size_t len) __TA_EXCLUDES(vfs_lock_);
    zx_status_t Ioctl(fbl::RefPtr<Vnode> vn, uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual) __TA_EXCLUDES(vfs_lock_);

#ifdef __Fuchsia__
    void TokenDiscard(zx::event ios_token) __TA_EXCLUDES(vfs_lock_);
    zx_status_t VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token,
                             zx::event* out) __TA_EXCLUDES(vfs_lock_);
    zx_status_t Link(zx::event token, fbl::RefPtr<Vnode> oldparent,
                     const char* oldname, const char* newname) __TA_EXCLUDES(vfs_lock_);
    zx_status_t Rename(zx::event token, fbl::RefPtr<Vnode> oldparent,
                       const char* oldname, const char* newname) __TA_EXCLUDES(vfs_lock_);
    // Calls readdir on the Vnode while holding the vfs_lock, preventing path
    // modification operations for the duration of the operation.
    zx_status_t Readdir(Vnode* vn, vdircookie_t* cookie,
                        void* dirents, size_t len) __TA_EXCLUDES(vfs_lock_);

    Vfs(async_t* async);

    async_t* async() { return async_; }
    void set_async(async_t* async) { async_ = async; }

    // Begins serving VFS messages over the specified connection.
    zx_status_t ServeConnection(fbl::unique_ptr<Connection> connection) __TA_EXCLUDES(vfs_lock_);

    // Called by a VFS connection when it is closed remotely.
    // The VFS is now responsible for destroying the connection.
    void OnConnectionClosedRemotely(Connection* connection) __TA_EXCLUDES(vfs_lock_);

    // Serves a Vnode over the specified channel (used for creating new filesystems)
    zx_status_t ServeDirectory(fbl::RefPtr<Vnode> vn, zx::channel channel);

    // Pins a handle to a remote filesystem onto a vnode, if possible.
    zx_status_t InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_EXCLUDES(vfs_lock_);

    // Create and mount a directory with a provided name
    zx_status_t MountMkdir(fbl::RefPtr<Vnode> vn,
                           const mount_mkdir_config_t* config) __TA_EXCLUDES(vfs_lock_);

    // Unpin a handle to a remote filesystem from a vnode, if one exists.
    zx_status_t UninstallRemote(fbl::RefPtr<Vnode> vn, zx::channel* h) __TA_EXCLUDES(vfs_lock_);

    // Unpins all remote filesystems in the current filesystem, and waits for the
    // response of each one with the provided deadline.
    zx_status_t UninstallAll(zx_time_t deadline) __TA_EXCLUDES(vfs_lock_);
#endif

private:
    // Walk from vn --> out until either only one path segment remains or we
    // encounter a remote filesystem.
    zx_status_t Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     const char* path, const char** pathout) __TA_REQUIRES(vfs_lock_);
    zx_status_t OpenLocked(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                           const char* path, const char** pathout,
                           uint32_t flags, uint32_t mode) __TA_REQUIRES(vfs_lock_);
#ifdef __Fuchsia__
    zx_status_t TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) __TA_REQUIRES(vfs_lock_);
    zx_status_t InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_REQUIRES(vfs_lock_);
    zx_status_t UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                      zx::channel* h) __TA_REQUIRES(vfs_lock_);
    // Waits for a remote handle on a Vnode to become ready to receive requests.
    // Returns |ZX_ERR_PEER_CLOSED| if the remote will never become available, since it is closed.
    // Returns |ZX_ERR_UNAVAILABLE| if there is no remote handle, or if the remote handle is not yet ready.
    // On success, returns the remote handle.
    zx_handle_t WaitForRemoteLocked(fbl::RefPtr<Vnode> vn) __TA_REQUIRES(vfs_lock_);

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
    MountNode::ListType remote_list_ __TA_GUARDED(vfs_lock_){};

    async_t* async_{};

protected:
    // A lock which should be used to protect lookup and walk operations
    mtx_t vfs_lock_{};

    // Starts tracking the lifetime of the connection.
    virtual void RegisterConnection(fbl::unique_ptr<Connection> connection);

    // Stops tracking the lifetime of the connection and destroys it.
    virtual void UnregisterAndDestroyConnection(Connection* connection);

#endif // ifdef __Fuchsia__
};

} // namespace fs
