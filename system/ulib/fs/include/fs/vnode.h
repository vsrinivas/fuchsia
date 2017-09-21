// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/vfs.h>
#include <fs/vfs.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#ifdef __Fuchsia__
#include <zx/channel.h>
#endif // __Fuchsia__

// VFS Helpers (vfs.c)
// clang-format off
#define VFS_FLAG_DEVICE          0x00000001
#define VFS_FLAG_MOUNT_READY     0x00000002
#define VFS_FLAG_DEVICE_DETACHED 0x00000004
#define VFS_FLAG_RESERVED_MASK   0x0000FFFF
// clang-format on

namespace fs {

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
    virtual ~Vnode();

#ifdef __Fuchsia__
    // Serves a connection to the Vnode over the specified channel.
    //
    // The default implementation creates and registers an RIO |Connection| with the VFS.
    // Subclasses may override this behavior to serve custom protocols over the channel.
    //
    // |vfs| is the VFS which manages the Vnode.
    // |channel| is the channel over which the client will exchange messages with the Vnode.
    // |flags| are the flags which were previously provided to |Open()|.
    virtual zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags);

    // Extract handle(s), type, and extra info from a vnode.
    // Returns the number of handles which should be returned on the requesting handle.
    virtual zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize);

    virtual zx_status_t WatchDir(Vfs* vfs, const vfs_watch_dir_t* cmd);
#endif

    virtual void Notify(const char* name, size_t len, unsigned event);

    // Ensure that it is valid to open vn.
    virtual zx_status_t Open(uint32_t flags) = 0;

    // Closes vn. Typically, most Vnodes simply return "ZX_OK".
    virtual zx_status_t Close();

    // Read data from vn at offset.
    //
    // If successful, returns the number of bytes read in |out_actual|. This must be
    // less than or equal to |len|.
    virtual zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual);

    // Write data to vn at offset.
    //
    // If successful, returns the number of bytes written in |out_actual|. This must be
    // less than or equal to |len|.
    virtual zx_status_t Write(const void* data, size_t len, size_t off, size_t* out_actual);

    // Attempt to find child of vn, child returned on success.
    // Name is len bytes long, and does not include a null terminator.
    virtual zx_status_t Lookup(fbl::RefPtr<Vnode>* out, const char* name, size_t len);

    // Read attributes of vn.
    virtual zx_status_t Getattr(vnattr_t* a);

    // Set attributes of vn.
    virtual zx_status_t Setattr(const vnattr_t* a);

    // Read directory entries of vn, error if not a directory.
    // FS-specific Cookie must be a buffer of vdircookie_t size or smaller.
    // Cookie must be zero'd before first call and will be used by.
    // the readdir implementation to maintain state across calls.
    // To "rewind" and start from the beginning, cookie may be zero'd.
    virtual zx_status_t Readdir(vdircookie_t* cookie, void* dirents, size_t len);

    // Create a new node under vn.
    // Name is len bytes long, and does not include a null terminator.
    // Mode specifies the type of entity to create.
    virtual zx_status_t Create(fbl::RefPtr<Vnode>* out, const char* name, size_t len, uint32_t mode);

    // Performs the given ioctl op on vn.
    // On success, returns the number of bytes received.
    virtual zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                              void* out_buf, size_t out_len, size_t* out_actual);

    // Removes name from directory vn
    virtual zx_status_t Unlink(const char* name, size_t len, bool must_be_dir);

    // Change the size of vn
    virtual zx_status_t Truncate(size_t len);

    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Called on the "olddir" vnode.
    // Unlinks any prior newname if it already exists.
    virtual zx_status_t Rename(fbl::RefPtr<Vnode> newdir,
                               const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen,
                               bool src_must_be_dir, bool dst_must_be_dir);

    // Creates a hard link to the 'target' vnode with a provided name in vndir
    virtual zx_status_t Link(const char* name, size_t len, fbl::RefPtr<Vnode> target);

    // Acquire a vmo from a vnode.
    //
    // At the moment, mmap can only map files from read-only filesystems,
    // since (without paging) there is no mechanism to update either
    // 1) The file by writing to the mapping, or
    // 2) The mapping by writing to the underlying file.
    virtual zx_status_t Mmap(int flags, size_t len, size_t* off, zx_handle_t* out);

    // Syncs the vnode with its underlying storage
    virtual zx_status_t Sync();

#ifdef __Fuchsia__
    // Attaches a handle to the vnode, if possible. Otherwise, returns an error.
    virtual zx_status_t AttachRemote(MountChannel h);

    // The following methods are required to mount sub-filesystems. The logic
    // (and storage) necessary to implement these functions exists within the
    // "RemoteContainer" class, which may be composed inside Vnodes that wish
    // to act as mount points.

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const;
    virtual zx::channel DetachRemote();
    virtual zx_handle_t WaitForRemote();
    virtual zx_handle_t GetRemote() const;
    virtual void SetRemote(zx::channel remote);

    // The vnode is a device. Devices may opt to reveal themselves as directories
    // or endpoints, depending on context. For the purposes of our VFS layer,
    // during path traversal, devices are NOT treated as mount points, even though
    // they contain remote handles.
    bool IsDevice() const { return (flags_ & VFS_FLAG_DEVICE) && IsRemote(); }
    void DetachDevice() {
        ZX_DEBUG_ASSERT(flags_ & VFS_FLAG_DEVICE);
        flags_ |= VFS_FLAG_DEVICE_DETACHED;
    }
    bool IsDetachedDevice() const { return (flags_ & VFS_FLAG_DEVICE_DETACHED); }
#endif

protected:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);

    Vnode();

    uint32_t flags_{};
};

// Helper class used to fill direntries during calls to Readdir.
class DirentFiller {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DirentFiller);

    DirentFiller(void* ptr, size_t len);

    // Attempts to add the name to the end of the dirent buffer
    // which is returned by readdir.
    zx_status_t Next(const char* name, size_t len, uint32_t type);

    zx_status_t BytesFilled() const {
        return static_cast<zx_status_t>(pos_);
    }

private:
    char* ptr_;
    size_t pos_;
    const size_t len_;
};

} // namespace fs
