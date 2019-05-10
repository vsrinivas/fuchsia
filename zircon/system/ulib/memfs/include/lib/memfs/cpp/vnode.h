// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#ifdef __cplusplus

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/managed-vfs.h>
#include <fs/remote.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fs/watcher.h>
#include <lib/zx/vmo.h>

#include <atomic>

namespace memfs {

constexpr uint64_t kMemfsBlksize = PAGE_SIZE;

class Dnode;
class Vfs;

class VnodeMemfs : public fs::Vnode {
public:
    virtual zx_status_t Setattr(const vnattr_t* a) final;
    virtual void Sync(SyncCallback closure) final;
    zx_status_t AttachRemote(fs::MountChannel h) final;

    // To be more specific: Is this vnode connected into the directory hierarchy?
    // VnodeDirs can be unlinked, and this method will subsequently return false.
    bool IsDirectory() const final { return dnode_ != nullptr; }
    void UpdateModified() {
        zx_time_t now = 0;
        zx_clock_get_new(ZX_CLOCK_UTC, &now);
        modify_time_ = now;
    }

    ~VnodeMemfs() override;

    Vfs* vfs() const { return vfs_; }
    uint64_t ino() const { return ino_; }

    fbl::RefPtr<Dnode> dnode_;
    uint32_t link_count_;

protected:
    explicit VnodeMemfs(Vfs* vfs);

    Vfs* vfs_;
    uint64_t ino_;
    uint64_t create_time_;
    uint64_t modify_time_;

    uint64_t GetInoCounter() const {
        return ino_ctr_.load(std::memory_order_relaxed);
    }

    uint64_t GetDeletedInoCounter() const {
        return deleted_ino_ctr_.load(std::memory_order_relaxed);
    }

private:
    static std::atomic<uint64_t> ino_ctr_;
    static std::atomic<uint64_t> deleted_ino_ctr_;
};

class VnodeFile final : public VnodeMemfs {
public:
    explicit VnodeFile(Vfs* vfs);
    ~VnodeFile() override;

    virtual zx_status_t ValidateFlags(uint32_t flags) final;

private:
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Write(const void* data, size_t len, size_t offset,
                      size_t* out_actual) final;
    zx_status_t Append(const void* data, size_t len, size_t* out_end,
                       size_t* out_actual) final;
    zx_status_t Truncate(size_t len) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;
    zx_status_t GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) final;

    // Ensure the underlying vmo is filled with zero from:
    // [start, round_up(end, PAGE_SIZE)).
    void ZeroTail(size_t start, size_t end);

    zx::vmo vmo_;
    // Cached length of the vmo.
    uint64_t vmo_size_;
    // Logical length of the underlying file.
    zx_off_t length_;
};

class VnodeDir final : public VnodeMemfs {
public:
    explicit VnodeDir(Vfs* vfs);
    ~VnodeDir() override;

    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) final;

    // Create a vnode from a VMO.
    // Fails if the vnode already exists.
    // Passes the vmo to the Vnode; does not duplicate it.
    zx_status_t CreateFromVmo(fbl::StringPiece name, zx_handle_t vmo,
                              zx_off_t off, zx_off_t len);

    // Mount a subtree as a child of this directory.
    void MountSubtree(fbl::RefPtr<VnodeDir> subtree);

    // Use the watcher container to implement a directory watcher
    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;
    zx_status_t QueryFilesystem(fuchsia_io_FilesystemInfo* out) final;

    // The vnode is acting as a mount point for a remote filesystem or device.
    bool IsRemote() const final;
    zx::channel DetachRemote() final;
    zx_handle_t GetRemote() const final;
    void SetRemote(zx::channel remote) final;

private:
    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                        size_t* out_actual) final;

    // Resolves the question, "Can this directory create a child node with the name?"
    // Returns "ZX_OK" on success; otherwise explains failure with error message.
    zx_status_t CanCreate(fbl::StringPiece name) const;

    // Creates a dnode for the Vnode, attaches vnode to dnode, (if directory) attaches
    // dnode to vnode, and adds dnode to parent directory.
    zx_status_t AttachVnode(fbl::RefPtr<memfs::VnodeMemfs> vn, fbl::StringPiece name,
                            bool isdir);

    zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
    zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, fbl::StringPiece oldname,
                       fbl::StringPiece newname, bool src_must_be_dir,
                       bool dst_must_be_dir) final;
    zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;
    zx_status_t GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) final;

    fs::RemoteContainer remoter_;
    fs::WatcherContainer watcher_;
};

class VnodeVmo final : public VnodeMemfs {
public:
    VnodeVmo(Vfs* vfs, zx_handle_t vmo, zx_off_t offset, zx_off_t length);
    ~VnodeVmo() override;

    virtual zx_status_t ValidateFlags(uint32_t flags) override;

private:
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;
    zx_status_t GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) final;
    zx_status_t MakeLocalClone(bool executable);

    zx_handle_t vmo_;
    zx_off_t offset_;
    zx_off_t length_;
    bool have_local_clone_;
};

class Vfs : public fs::ManagedVfs {
public:
    // Creates a Vfs with practically unlimited pages upper bound.
    Vfs()
        : fs::ManagedVfs(), pages_limit_(UINT64_MAX), num_allocated_pages_(0) {}

    // Creates a Vfs with the maximum |pages_limit| number of pages.
    explicit Vfs(size_t pages_limit)
        : fs::ManagedVfs(), pages_limit_(pages_limit), num_allocated_pages_(0) {}

    // Creates a VnodeVmo under |parent| with |name| which is backed by |vmo|.
    // N.B. The VMO will not be taken into account when calculating
    // number of allocated pages in this Vfs.
    zx_status_t CreateFromVmo(VnodeDir* parent, fbl::StringPiece name,
                              zx_handle_t vmo, zx_off_t off,
                              zx_off_t len);

    void MountSubtree(VnodeDir* parent, fbl::RefPtr<VnodeDir> subtree);

    size_t PagesLimit() const { return pages_limit_; }

    size_t NumAllocatedPages() const { return num_allocated_pages_; }

    uint64_t GetFsId() const { return fs_id_; }

private:
    // Initialize fs_id_ on the first call.
    // Calling more than once is a no-op.
    zx_status_t FillFsId();

    friend zx_status_t CreateFilesystem(const char* name, memfs::Vfs* vfs,
                                        fbl::RefPtr<VnodeDir>* out);

    // Allows VnodeFile (and no other class) to manipulate number of allocated pages
    // using GrowVMO and WillFreeVMO.
    friend VnodeFile;

    // Increases the size of the |vmo| to at least |request_size| bytes.
    // If the VMO is invalid, it will try to create it.
    // |current_size| is the current size of the VMO in number of bytes. It should be
    // a multiple of page size. The new size of the VMO is returned via |actual_size|.
    // If the new size would cause us to exceed the limit on number of pages or if the system
    // ran out of memory, an error is returned.
    zx_status_t GrowVMO(zx::vmo& vmo, size_t current_size,
                        size_t request_size, size_t* actual_size);

    // VnodeFile must call this function in the destructor to signal that its VMO will be freed.
    // |vmo_size| is the size of the owned vmo in bytes. It should be a multiple of page size.
    void WillFreeVMO(size_t vmo_size);

    // Maximum number of pages available; fixed at Vfs creation time.
    // Puts a bound on maximum memory usage.
    const size_t pages_limit_;

    // Number of pages currently in use by VnodeFiles.
    size_t num_allocated_pages_;

    uint64_t fs_id_ = 0;
};

// Initializes the Vfs object and names the root directory |name|. The Vfs object is considered
// invalid prior to this call. Returns the root VnodeDir via |out|.
zx_status_t CreateFilesystem(const char* name, memfs::Vfs* vfs, fbl::RefPtr<VnodeDir>* out);

} // namespace memfs

#endif // ifdef __cplusplus
