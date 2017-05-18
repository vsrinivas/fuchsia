// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "trace.h"

#include <mxio/remoteio.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/vfs.h>

#ifdef __Fuchsia__
#include <threads.h>
#include <mxio/io.h>
#endif

#define NO_DOTDOT true

// VFS Helpers (vfs.c)
#define V_FLAG_DEVICE                 1
#define V_FLAG_MOUNT_READY            2
#define V_FLAG_DEVICE_DETACHED        4
#define V_FLAG_RESERVED_MASK 0x0000FFFF

__BEGIN_CDECLS
// A lock which should be used to protect lookup and walk operations
#ifdef __Fuchsia__
extern mtx_t vfs_lock;
#endif

typedef struct vfs_iostate vfs_iostate_t;

__END_CDECLS

#ifdef __cplusplus

#ifdef __Fuchsia__
#include <fs/dispatcher.h>
#include <mxtl/mutex.h>
#endif  // __Fuchsia__

#include <mxtl/intrusive_double_list.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

namespace fs {

// RemoteContainer adds support for mounting remote handles on nodes.
class RemoteContainer {
public:
    bool IsRemote() const;
    mx_handle_t DetachRemote(uint32_t &flags_);
    // Access the remote handle if it's ready -- otherwise, return an error.
    mx_handle_t WaitForRemote(uint32_t &flags_);
    mx_handle_t GetRemote() const;
    void SetRemote(mx_handle_t remote);
    constexpr RemoteContainer() : remote_(MX_HANDLE_INVALID) {};
private:
    mx_handle_t remote_;
};

#ifdef __Fuchsia__

struct VnodeWatcher : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<VnodeWatcher>> {
public:
    VnodeWatcher();
    ~VnodeWatcher();

    mx_handle_t h;
};

class WatcherContainer {
public:
    virtual mx_status_t WatchDir(mx_handle_t* out) final;
    virtual void NotifyAdd(const char* name, size_t len) final;
private:
    mxtl::Mutex lock_;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<VnodeWatcher>> watch_list_ __TA_GUARDED(lock_);
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

// The VFS interface declares a default abtract Vnode class with
// common operations that may be overwritten.
//
// The ops are used for dispatch and the lifecycle of Vnodes are owned
// by RefPtrs.
//
// The lower half of flags (V_FLAG_RESERVED_MASK) is reserved
// for usage by fs::Vnode, but the upper half of flags may
// be used by subclasses of Vnode.

class Vnode : public mxtl::RefCounted<Vnode> {
public:
#ifdef __Fuchsia__
    // Allocate iostate and register the transferred handle with a dispatcher.
    // Allows Vnode to act as server.
    //
    // Serve ALWAYS consumes 'h'.
    virtual mx_status_t Serve(mx_handle_t h, uint32_t flags);

    // Extract handle(s), type, and extra info from a vnode.
    // Returns the number of handles which should be returned on the requesting handle.
    virtual mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize) {
        *type = MXIO_PROTOCOL_REMOTE;
        return 0;
    }
#endif

    virtual mx_status_t WatchDir(mx_handle_t* out) { return ERR_NOT_SUPPORTED; }
    virtual void NotifyAdd(const char* name, size_t len) {}

    // Ensure that it is valid to open vn.
    virtual mx_status_t Open(uint32_t flags) = 0;

    // Closes vn. Typically, most Vnodes simply return "NO_ERROR".
    virtual mx_status_t Close();

    // Read data from vn at offset.
    virtual ssize_t Read(void* data, size_t len, size_t off) {
        return ERR_NOT_SUPPORTED;
    }

    // Write data to vn at offset.
    virtual ssize_t Write(const void* data, size_t len, size_t off) {
        return ERR_NOT_SUPPORTED;
    }

    // Attempt to find child of vn, child returned on success.
    // Name is len bytes long, and does not include a null terminator.
    virtual mx_status_t Lookup(mxtl::RefPtr<Vnode>* out, const char* name, size_t len) {
        return ERR_NOT_SUPPORTED;
    }

    // Read attributes of vn.
    virtual mx_status_t Getattr(vnattr_t* a) {
        return ERR_NOT_SUPPORTED;
    }

    // Set attributes of vn.
    virtual mx_status_t Setattr(vnattr_t* a) {
        return ERR_NOT_SUPPORTED;
    }

    // Read directory entries of vn, error if not a directory.
    // FS-specific Cookie must be a buffer of vdircookie_t size or smaller.
    // Cookie must be zero'd before first call and will be used by.
    // the readdir implementation to maintain state across calls.
    // To "rewind" and start from the beginning, cookie may be zero'd.
    virtual mx_status_t Readdir(void* cookie, void* dirents, size_t len) {
        return ERR_NOT_SUPPORTED;
    }

    // Create a new node under vn.
    // Name is len bytes long, and does not include a null terminator.
    // Mode specifies the type of entity to create.
    virtual mx_status_t Create(mxtl::RefPtr<Vnode>* out, const char* name, size_t len, uint32_t mode) {
        return ERR_NOT_SUPPORTED;
    }

    // Performs the given ioctl op on vn.
    // On success, returns the number of bytes received.
    virtual ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len) {
        return ERR_NOT_SUPPORTED;
    }

    // Removes name from directory vn
    virtual mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) {
        return ERR_NOT_SUPPORTED;
    }

    // Change the size of vn
    virtual mx_status_t Truncate(size_t len) {
        return ERR_NOT_SUPPORTED;
    }

    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Called on the "olddir" vnode.
    // Unlinks any prior newname if it already exists.
    virtual mx_status_t Rename(mxtl::RefPtr<Vnode> newdir,
                               const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen,
                               bool src_must_be_dir, bool dst_must_be_dir) {
        return ERR_NOT_SUPPORTED;
    }

    // Creates a hard link to the 'target' vnode with a provided name in vndir
    virtual mx_status_t Link(const char* name, size_t len, mxtl::RefPtr<Vnode> target) {
        return ERR_NOT_SUPPORTED;
    }

    // Acquire a vmo from a vnode.
    //
    // At the moment, mmap can only map files from read-only filesystems,
    // since (without paging) there is no mechanism to update either
    // 1) The file by writing to the mapping, or
    // 2) The mapping by writing to the underlying file.
    virtual mx_status_t Mmap(int flags, size_t len, size_t* off, mx_handle_t* out) {
        return ERR_NOT_SUPPORTED;
    }

    // Syncs the vnode with its underlying storage
    virtual mx_status_t Sync() {
        return ERR_NOT_SUPPORTED;
    }

    virtual ~Vnode() {};

#ifdef __Fuchsia__
    virtual Dispatcher* GetDispatcher() = 0;
#endif

    // Attaches a handle to the vnode, if possible. Otherwise, returns an error.
    virtual mx_status_t AttachRemote(mx_handle_t h) { return ERR_NOT_SUPPORTED; }

    // The following methods are required to mount sub-filesystems. The logic
    // (and storage) necessary to implement these functions exists within the
    // "RemoteContainer" class, which may be composed inside Vnodes that wish
    // to act as mount points.

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const { return false; }
    virtual mx_handle_t DetachRemote() { return MX_HANDLE_INVALID; }
    virtual mx_handle_t WaitForRemote() { return ERR_UNAVAILABLE; }
    virtual mx_handle_t GetRemote() const { return MX_HANDLE_INVALID; }
    virtual void SetRemote(mx_handle_t remote) { MX_DEBUG_ASSERT(false); }

    // The vnode is a device. Devices may opt to reveal themselves as directories
    // or endpoints, depending on context. For the purposes of our VFS layer,
    // during path traversal, devices are NOT treated as mount points, even though
    // they contain remote handles.
    bool IsDevice() const { return (flags_ & V_FLAG_DEVICE) && IsRemote(); }
    void DetachDevice() {
        MX_DEBUG_ASSERT(flags_ & V_FLAG_DEVICE);
        flags_ |= V_FLAG_DEVICE_DETACHED;
    }
    bool IsDetachedDevice() const { return (flags_ & V_FLAG_DEVICE_DETACHED); }
protected:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);
    Vnode() : flags_(0) {};

    uint32_t flags_;
};

struct Vfs {
    // Walk from vn --> out until either only one path segment remains or we
    // encounter a remote filesystem.
    static mx_status_t Walk(mxtl::RefPtr<Vnode> vn, mxtl::RefPtr<Vnode>* out,
                            const char* path, const char** pathout);
    // Traverse the path to the target vnode, and create / open it using
    // the underlying filesystem functions (lookup, create, open).
    static mx_status_t Open(mxtl::RefPtr<Vnode> vn, mxtl::RefPtr<Vnode>* out,
                            const char* path, const char** pathout,
                            uint32_t flags, uint32_t mode);
    static mx_status_t Unlink(mxtl::RefPtr<Vnode> vn, const char* path, size_t len);
    static mx_status_t Link(mxtl::RefPtr<Vnode> oldparent, mxtl::RefPtr<Vnode> newparent,
                            const char* oldname, const char* newname);
    static mx_status_t Rename(mxtl::RefPtr<Vnode> oldparent, mxtl::RefPtr<Vnode> newparent,
                              const char* oldname, const char* newname);
    static ssize_t Ioctl(mxtl::RefPtr<Vnode> vn, uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len);

#ifdef __Fuchsia__
    // Pins a handle to a remote filesystem onto a vnode, if possible.
    static mx_status_t InstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t h);
    static mx_status_t InstallRemoteLocked(mxtl::RefPtr<Vnode> vn, mx_handle_t h) __TA_REQUIRES(vfs_lock);
    // Unpin a handle to a remote filesystem from a vnode, if one exists.
    static mx_status_t UninstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t* h);
#endif  // ifdef __Fuchsia__
};

} // namespace fs

// vfs dispatch  (NOTE: only used for mounted roots)
mx_handle_t vfs_rpc_server(mx_handle_t h, mxtl::RefPtr<fs::Vnode> vn);

using Vnode = fs::Vnode;

#else  // ifdef __cplusplus

typedef struct Vnode Vnode;

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

// Send an unmount signal on a handle to a filesystem and await a response.
mx_status_t vfs_unmount_handle(mx_handle_t h, mx_time_t deadline);

// Unpins all remote filesystems in the current filesystem, and waits for the
// response of each one with the provided deadline.
mx_status_t vfs_uninstall_all(mx_time_t deadline);

__END_CDECLS
