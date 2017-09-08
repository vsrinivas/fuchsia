// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "trace.h"

#include <mxio/remoteio.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/types.h>

#include <mxio/vfs.h>

#ifdef __Fuchsia__
#include <threads.h>
#include <mxio/io.h>
#endif

// VFS Helpers (vfs.c)
// clang-format off
#define VFS_FLAG_DEVICE          0x00000001
#define VFS_FLAG_MOUNT_READY     0x00000002
#define VFS_FLAG_DEVICE_DETACHED 0x00000004
#define VFS_FLAG_RESERVED_MASK   0x0000FFFF
// clang-format on

__BEGIN_CDECLS

typedef struct vfs_iostate vfs_iostate_t;

// Send an unmount signal on a handle to a filesystem and await a response.
mx_status_t vfs_unmount_handle(mx_handle_t h, mx_time_t deadline);

__END_CDECLS

#ifdef __cplusplus

#ifdef __Fuchsia__
#include <fs/dispatcher.h>
#include <mx/channel.h>
#include <mx/event.h>
#include <mx/vmo.h>
#include <fbl/mutex.h>
#endif  // __Fuchsia__

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

namespace fs {

class Vnode;
class Vfs;

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
    explicit MountChannel(mx_handle_t handle) : channel_(handle) {}
    explicit MountChannel(mx::channel channel) : channel_(fbl::move(channel)) {}
    MountChannel(MountChannel&& other) : channel_(fbl::move(other.channel_)) {}

    mx::channel TakeChannel() { return fbl::move(channel_); }

    ~MountChannel() {
        if (channel_.is_valid()) {
            vfs_unmount_handle(channel_.release(), 0);
        }
    }

private:
    mx::channel channel_;
};

#endif // __Fuchsia__

// Helper class used to fill direntries during calls to Readdir.
class DirentFiller {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DirentFiller);

    DirentFiller(void* ptr, size_t len);

    // Attempts to add the name to the end of the dirent buffer
    // which is returned by readdir.
    mx_status_t Next(const char* name, size_t len, uint32_t type);

    mx_status_t BytesFilled() const {
        return static_cast<mx_status_t>(pos_);
    }

private:
    char* ptr_;
    size_t pos_;
    const size_t len_;
};

inline bool vfs_valid_name(const char* name, size_t len) {
    return (len <= NAME_MAX &&
            memchr(name, '/', len) == nullptr &&
            (len != 1 || strncmp(name, ".", 1)) &&
            (len != 2 || strncmp(name, "..", 2)));
}

// The VFS interface declares a default abtract Vnode class with
// common operations that may be overwritten.
//
// The ops are used for dispatch and the lifecycle of Vnodes are owned
// by RefPtrs.
//
// All names passed to the Vnode class are valid according to "vfs_valid_name".
//
// The lower half of flags (VFS_FLAG_RESERVED_MASK) is reserved
// for usage by fs::Vnode, but the upper half of flags may
// be used by subclasses of Vnode.
class Vnode : public fbl::RefCounted<Vnode> {
public:
#ifdef __Fuchsia__
    // Allocate iostate and register the transferred handle with a dispatcher.
    // Allows Vnode to act as server.
    virtual mx_status_t Serve(fs::Vfs* vfs, mx::channel channel, uint32_t flags);

    // Extract handle(s), type, and extra info from a vnode.
    // Returns the number of handles which should be returned on the requesting handle.
    virtual mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize) {
        *type = MXIO_PROTOCOL_REMOTE;
        return 0;
    }

    virtual mx_status_t WatchDir(mx::channel* out) { return MX_ERR_NOT_SUPPORTED; }
    virtual mx_status_t WatchDirV2(Vfs* vfs, const vfs_watch_dir_t* cmd) {
        return MX_ERR_NOT_SUPPORTED;
    }
#endif
    virtual void Notify(const char* name, size_t len, unsigned event) {}

    // Ensure that it is valid to open vn.
    virtual mx_status_t Open(uint32_t flags) = 0;

    // Closes vn. Typically, most Vnodes simply return "MX_OK".
    virtual mx_status_t Close();

    // Read data from vn at offset.
    virtual ssize_t Read(void* data, size_t len, size_t off) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Write data to vn at offset.
    virtual ssize_t Write(const void* data, size_t len, size_t off) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Attempt to find child of vn, child returned on success.
    // Name is len bytes long, and does not include a null terminator.
    virtual mx_status_t Lookup(fbl::RefPtr<Vnode>* out, const char* name, size_t len) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Read attributes of vn.
    virtual mx_status_t Getattr(vnattr_t* a) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Set attributes of vn.
    virtual mx_status_t Setattr(vnattr_t* a) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Read directory entries of vn, error if not a directory.
    // FS-specific Cookie must be a buffer of vdircookie_t size or smaller.
    // Cookie must be zero'd before first call and will be used by.
    // the readdir implementation to maintain state across calls.
    // To "rewind" and start from the beginning, cookie may be zero'd.
    virtual mx_status_t Readdir(void* cookie, void* dirents, size_t len) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Create a new node under vn.
    // Name is len bytes long, and does not include a null terminator.
    // Mode specifies the type of entity to create.
    virtual mx_status_t Create(fbl::RefPtr<Vnode>* out, const char* name, size_t len, uint32_t mode) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Performs the given ioctl op on vn.
    // On success, returns the number of bytes received.
    virtual ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Removes name from directory vn
    virtual mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Change the size of vn
    virtual mx_status_t Truncate(size_t len) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Called on the "olddir" vnode.
    // Unlinks any prior newname if it already exists.
    virtual mx_status_t Rename(fbl::RefPtr<Vnode> newdir,
                               const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen,
                               bool src_must_be_dir, bool dst_must_be_dir) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Creates a hard link to the 'target' vnode with a provided name in vndir
    virtual mx_status_t Link(const char* name, size_t len, fbl::RefPtr<Vnode> target) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Acquire a vmo from a vnode.
    //
    // At the moment, mmap can only map files from read-only filesystems,
    // since (without paging) there is no mechanism to update either
    // 1) The file by writing to the mapping, or
    // 2) The mapping by writing to the underlying file.
    virtual mx_status_t Mmap(int flags, size_t len, size_t* off, mx_handle_t* out) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Syncs the vnode with its underlying storage
    virtual mx_status_t Sync() {
        return MX_ERR_NOT_SUPPORTED;
    }

    virtual ~Vnode() {};

#ifdef __Fuchsia__
    // Attaches a handle to the vnode, if possible. Otherwise, returns an error.
    virtual mx_status_t AttachRemote(MountChannel h) { return MX_ERR_NOT_SUPPORTED; }

    // The following methods are required to mount sub-filesystems. The logic
    // (and storage) necessary to implement these functions exists within the
    // "RemoteContainer" class, which may be composed inside Vnodes that wish
    // to act as mount points.

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const { return false; }
    virtual mx::channel DetachRemote() { return mx::channel(); }
    virtual mx_handle_t WaitForRemote() { return MX_HANDLE_INVALID; }
    virtual mx_handle_t GetRemote() const { return MX_HANDLE_INVALID; }
    virtual void SetRemote(mx::channel remote) { MX_DEBUG_ASSERT(false); }

    // The vnode is a device. Devices may opt to reveal themselves as directories
    // or endpoints, depending on context. For the purposes of our VFS layer,
    // during path traversal, devices are NOT treated as mount points, even though
    // they contain remote handles.
    bool IsDevice() const { return (flags_ & VFS_FLAG_DEVICE) && IsRemote(); }
    void DetachDevice() {
        MX_DEBUG_ASSERT(flags_ & VFS_FLAG_DEVICE);
        flags_ |= VFS_FLAG_DEVICE_DETACHED;
    }
    bool IsDetachedDevice() const { return (flags_ & VFS_FLAG_DEVICE_DETACHED); }
#endif
protected:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);
    Vnode() : flags_(0) {};

    uint32_t flags_;
};

#ifdef __Fuchsia__
// Non-intrusive node in linked list of vnodes acting as mount points
class MountNode final : public fbl::DoublyLinkedListable<fbl::unique_ptr<MountNode>> {
public:
    using ListType = fbl::DoublyLinkedList<fbl::unique_ptr<MountNode>>;
    constexpr MountNode() : vn_(nullptr) {}
    ~MountNode() { MX_DEBUG_ASSERT(vn_ == nullptr); }

    void SetNode(fbl::RefPtr<Vnode> vn) {
        MX_DEBUG_ASSERT(vn_ == nullptr);
        vn_ = vn;
    }

    mx::channel ReleaseRemote() {
        MX_DEBUG_ASSERT(vn_ != nullptr);
        mx::channel h = vn_->DetachRemote();
        vn_ = nullptr;
        return h;
    }

    bool VnodeMatch(fbl::RefPtr<Vnode> vn) const {
        MX_DEBUG_ASSERT(vn_ != nullptr);
        return vn == vn_;
    }

private:
    fbl::RefPtr<Vnode> vn_;
};

#endif

// The Vfs object contains global per-filesystem state, which
// may be valid across a collection of Vnodes.
//
// The Vfs object must outlive the Vnodes which it serves.
class Vfs {
public:
    Vfs();
    // Walk from vn --> out until either only one path segment remains or we
    // encounter a remote filesystem.
    mx_status_t Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     const char* path, const char** pathout) __TA_REQUIRES(vfs_lock_);
    // Traverse the path to the target vnode, and create / open it using
    // the underlying filesystem functions (lookup, create, open).
    mx_status_t Open(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                     const char* path, const char** pathout,
                     uint32_t flags, uint32_t mode) __TA_EXCLUDES(vfs_lock_);
    mx_status_t Unlink(fbl::RefPtr<Vnode> vn, const char* path, size_t len) __TA_EXCLUDES(vfs_lock_);
    ssize_t Ioctl(fbl::RefPtr<Vnode> vn, uint32_t op, const void* in_buf, size_t in_len,
                  void* out_buf, size_t out_len) __TA_EXCLUDES(vfs_lock_);

#ifdef __Fuchsia__
    void TokenDiscard(mx::event* ios_token) __TA_EXCLUDES(vfs_lock_);
    mx_status_t VnodeToToken(fbl::RefPtr<Vnode> vn, mx::event* ios_token,
                             mx::event* out) __TA_EXCLUDES(vfs_lock_);
    mx_status_t Link(mx::event token, fbl::RefPtr<Vnode> oldparent,
                     const char* oldname, const char* newname) __TA_EXCLUDES(vfs_lock_);
    mx_status_t Rename(mx::event token, fbl::RefPtr<Vnode> oldparent,
                       const char* oldname, const char* newname) __TA_EXCLUDES(vfs_lock_);

    Vfs(Dispatcher* dispatcher);

    void SetDispatcher(Dispatcher* dispatcher) { dispatcher_ = dispatcher; }

    // Dispatches to a Vnode over the specified handle (normal case)
    mx_status_t Serve(mx::channel channel, void* ios);

    // Serves a Vnode over the specified handle (used for creating new filesystems)
    mx_status_t ServeDirectory(fbl::RefPtr<fs::Vnode> vn, mx::channel channel);

    // Pins a handle to a remote filesystem onto a vnode, if possible.
    mx_status_t InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_EXCLUDES(vfs_lock_);

    // Create and mount a directory with a provided name
    mx_status_t MountMkdir(fbl::RefPtr<Vnode> vn,
                           const mount_mkdir_config_t* config) __TA_EXCLUDES(vfs_lock_);

    // Unpin a handle to a remote filesystem from a vnode, if one exists.
    mx_status_t UninstallRemote(fbl::RefPtr<Vnode> vn, mx::channel* h) __TA_EXCLUDES(vfs_lock_);

    // Unpins all remote filesystems in the current filesystem, and waits for the
    // response of each one with the provided deadline.
    mx_status_t UninstallAll(mx_time_t deadline) __TA_EXCLUDES(vfs_lock_);

    // A lock which should be used to protect lookup and walk operations
    // TODO(smklein): Encapsulate the lock; make it private.
    mtx_t vfs_lock_{};
#endif

private:
    mx_status_t OpenLocked(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out,
                           const char* path, const char** pathout,
                           uint32_t flags, uint32_t mode) __TA_REQUIRES(vfs_lock_);
#ifdef __Fuchsia__
    mx_status_t TokenToVnode(mx::event token, fbl::RefPtr<Vnode>* out) __TA_REQUIRES(vfs_lock_);
    mx_status_t InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_REQUIRES(vfs_lock_);
    mx_status_t UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                      mx::channel* h) __TA_REQUIRES(vfs_lock_);
    // Waits for a remote handle on a Vnode to become ready to receive requests.
    // Returns |MX_ERR_PEER_CLOSED| if the remote will never become available, since it is closed.
    // Returns |MX_ERR_UNAVAILABLE| if there is no remote handle, or if the remote handle is not yet ready.
    // On success, returns the remote handle.
    mx_handle_t WaitForRemoteLocked(fbl::RefPtr<Vnode> vn) __TA_REQUIRES(vfs_lock_);
    // The mount list is a global static variable, but it only uses
    // constexpr constructors during initialization. As a consequence,
    // the .init_array section of the compiled vfs-mount object file is
    // empty; "remote_list" is a member of the bss section.
    MountNode::ListType remote_list_ __TA_GUARDED(vfs_lock_){};

    Dispatcher* dispatcher_{};
#endif  // ifdef __Fuchsia__
};

} // namespace fs

#endif // ifdef __cplusplus

__BEGIN_CDECLS

typedef struct vnattr vnattr_t;
typedef struct vdirent vdirent_t;

typedef struct vdircookie {
    uint64_t n;
    void* p;
} vdircookie_t;

// Handle incoming mxrio messages, dispatching them to vnode operations.
mx_status_t vfs_handler(mxrio_msg_t* msg, void* cookie);

__END_CDECLS
