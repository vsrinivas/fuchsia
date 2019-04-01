// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_VNODE_H_
#define FS_VNODE_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted_internal.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fs/mount_channel.h>
#include <fs/ref_counted.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <utility>

#ifdef __Fuchsia__
#include <fuchsia/io/c/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>
#endif // __Fuchsia__

namespace fs {

class Vfs;
typedef struct vdircookie vdircookie_t;

inline bool vfs_valid_name(fbl::StringPiece name) {
    return name.length() <= NAME_MAX &&
           memchr(name.data(), '/', name.length()) == nullptr &&
           name != "." && name != "..";
}

// The VFS interface declares a default abstract Vnode class with
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
class Vnode : public VnodeRefCounted<Vnode>, public fbl::Recyclable<Vnode> {
public:
    virtual ~Vnode();
    virtual void fbl_recycle() { delete this; }

    // Ensures that it is valid to access the vnode with given flags.
    virtual zx_status_t ValidateFlags(uint32_t flags);

    // Provides an opportunity to redirect subsequent I/O operations to a
    // different vnode.
    //
    // Flags will have already been validated by "ValidateFlags".
    // Open should never be invoked if flags includes "O_PATH".
    //
    // If the implementation of |Open()| sets |out_redirect| to a non-null value.
    // all following I/O operations on the opened file will be redirected to the
    // indicated vnode instead of being handled by this instance.
    //
    // |flags| are the open flags to be validated, such as |ZX_FS_RIGHT_READABLE| and
    // |ZX_FS_FLAG_DIRECTORY|.
    virtual zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect);

    // METHODS FOR OPENED NODES
    //
    // The following operations will not be invoked unless the Vnode has
    // been "Open()"-ed successfully.
    //
    // For files opened with O_PATH (as a file descriptor only) the base
    // classes' implementation of some of these functions may be invoked anyway.

#ifdef __Fuchsia__
    // Serves a connection to the Vnode over the specified channel.
    //
    // The default implementation creates and registers a FIDL |Connection| with the VFS.
    // Subclasses may override this behavior to serve custom protocols over the channel.
    //
    // |vfs| is the VFS which manages the Vnode.
    // |channel| is the channel over which the client will exchange messages with the Vnode.
    // |flags| are the flags and rights which were previously provided to |Open()|.
    virtual zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags);

    // Extract handle, type, and extra info from a vnode.
    virtual zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) = 0;

    virtual zx_status_t WatchDir(Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher);
#endif

    // Closes vn. Will be called once for each successful Open().
    //
    // Typically, most Vnodes simply return "ZX_OK".
    virtual zx_status_t Close();

    // Read data from vn at offset.
    //
    // If successful, returns the number of bytes read in |out_actual|. This must be
    // less than or equal to |len|.
    virtual zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual);

    // Write |len| bytes of |data| to the file, starting at |offset|.
    //
    // If successful, returns the number of bytes written in |out_actual|. This must be
    // less than or equal to |len|.
    virtual zx_status_t Write(const void* data, size_t len, size_t offset,
                              size_t* out_actual);

    // Write |len| bytes of |data| to the end of the file.
    //
    // If successful, returns the number of bytes written in |out_actual|, and
    // returns the new end of file offset in |out_end|.
    virtual zx_status_t Append(const void* data, size_t len, size_t* out_end,
                               size_t* out_actual);

    // Change the size of vn
    virtual zx_status_t Truncate(size_t len);

    // Set attributes of vn.
    virtual zx_status_t Setattr(const vnattr_t* a);

    // Acquire a vmo from a vnode.
    //
    // At the moment, mmap can only map files from read-only filesystems,
    // since (without paging) there is no mechanism to update either
    // 1) The file by writing to the mapping, or
    // 2) The mapping by writing to the underlying file.
    virtual zx_status_t GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size);

    // Syncs the vnode with its underlying storage.
    //
    // Returns the result status through a closure.
    using SyncCallback = fbl::Function<void(zx_status_t status)>;
    virtual void Sync(SyncCallback closure);

    // Read directory entries of vn, error if not a directory.
    // FS-specific Cookie must be a buffer of vdircookie_t size or smaller.
    // Cookie must be zero'd before first call and will be used by
    // the readdir implementation to maintain state across calls.
    // To "rewind" and start from the beginning, cookie may be zero'd.
    virtual zx_status_t Readdir(vdircookie_t* cookie, void* dirents, size_t len,
                                size_t* out_actual);

    // METHODS FOR OPENED OR UNOPENED NODES
    //
    // The following operations may be invoked on a Vnode, even if it has
    // not been "Open()"-ed.

    // Attempt to find child of vn, child returned on success.
    // Name is len bytes long, and does not include a null terminator.
    virtual zx_status_t Lookup(fbl::RefPtr<Vnode>* out, fbl::StringPiece name);

    // Read attributes of vn.
    virtual zx_status_t Getattr(vnattr_t* a);

    // Create a new node under vn.
    // Name is len bytes long, and does not include a null terminator.
    // Mode specifies the type of entity to create.
    virtual zx_status_t Create(fbl::RefPtr<Vnode>* out, fbl::StringPiece name, uint32_t mode);

    // Removes name from directory vn
    virtual zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir);

    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Called on the "olddir" vnode.
    // Unlinks any prior newname if it already exists.
    virtual zx_status_t Rename(fbl::RefPtr<Vnode> newdir,
                               fbl::StringPiece oldname, fbl::StringPiece newname,
                               bool src_must_be_dir, bool dst_must_be_dir);

    // Creates a hard link to the 'target' vnode with a provided name in vndir
    virtual zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<Vnode> target);

    // Invoked by the VFS layer whenever files are added or removed.
    virtual void Notify(fbl::StringPiece name, unsigned event);

#ifdef __Fuchsia__
    // Return information about the underlying filesystem, if desired.
    virtual zx_status_t QueryFilesystem(fuchsia_io_FilesystemInfo* out);

    // Returns the name of the device backing the filesystem, if one exists.
    virtual zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len);

    // Attaches a handle to the vnode, if possible. Otherwise, returns an error.
    virtual zx_status_t AttachRemote(MountChannel h);

    // The following methods are required to mount sub-filesystems. The logic
    // (and storage) necessary to implement these functions exists within the
    // "RemoteContainer" class, which may be composed inside Vnodes that wish
    // to act as mount points.

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const;
    virtual zx::channel DetachRemote();
    virtual zx_handle_t GetRemote() const;
    virtual void SetRemote(zx::channel remote);
#endif

    // Return if the Vnode is a directory; as a guideline, a directory Vnode should
    // support |Lookup| and opening a child Vnode.
    virtual bool IsDirectory() const = 0;

protected:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);
    Vnode();
};

// Opens a vnode by reference.
// The |vnode| reference is updated in-place if redirection occurs.
inline zx_status_t OpenVnode(uint32_t flags, fbl::RefPtr<Vnode>* vnode) {
    fbl::RefPtr<Vnode> redirect;
    zx_status_t status = (*vnode)->Open(flags, &redirect);
    if (status == ZX_OK && redirect != nullptr) {
        *vnode = std::move(redirect);
    }
    return status;
}

// Helper class used to fill direntries during calls to Readdir.
class DirentFiller {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DirentFiller);

    DirentFiller(void* ptr, size_t len);

    // Attempts to add the name to the end of the dirent buffer
    // which is returned by readdir.
    zx_status_t Next(fbl::StringPiece name, uint8_t type, uint64_t ino);

    zx_status_t BytesFilled() const {
        return static_cast<zx_status_t>(pos_);
    }

private:
    char* ptr_;
    size_t pos_;
    const size_t len_;
};

// Helper class to track outstanding operations associated to a
// particular Vnode.
class VnodeToken : public fbl::SinglyLinkedListable<std::unique_ptr<VnodeToken>> {
public:
    VnodeToken(zx_koid_t koid, fbl::RefPtr<Vnode> vnode)
        : koid_(koid), vnode_(std::move(vnode)) {
    }

    zx_koid_t get_koid() const { return koid_; }
    fbl::RefPtr<Vnode> get_vnode() const { return vnode_; }

    // Trait implementation for fbl::HashTable
    zx_koid_t GetKey() const { return koid_; }
    static size_t GetHash(zx_koid_t koid) { return koid; }

private:
    zx_koid_t koid_;
    fbl::RefPtr<Vnode> vnode_;
};

} // namespace fs

#endif // FS_VNODE_H_
