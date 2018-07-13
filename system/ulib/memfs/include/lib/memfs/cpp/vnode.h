// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>

#ifdef __cplusplus

#include <fs/managed-vfs.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/remote.h>
#include <fs/watcher.h>
#include <lib/zx/vmo.h>

namespace memfs {

constexpr uint64_t kMemfsBlksize = PAGE_SIZE;

class Dnode;
class Vfs;

class VnodeMemfs : public fs::Vnode {
public:
    virtual zx_status_t Setattr(const vnattr_t* a) final;
    virtual void Sync(SyncCallback closure) final;
    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual) final;
    zx_status_t AttachRemote(fs::MountChannel h) final;

    // To be more specific: Is this vnode connected into the directory hierarchy?
    // VnodeDirs can be unlinked, and this method will subsequently return false.
    bool IsDirectory() const { return dnode_ != nullptr; }
    void UpdateModified() { modify_time_ = zx_clock_get(ZX_CLOCK_UTC); }

    virtual ~VnodeMemfs();

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

private:
    static fbl::atomic<uint64_t> ino_ctr_;
};

class VnodeFile final : public VnodeMemfs {
public:
    VnodeFile(Vfs* vfs);
    VnodeFile(Vfs* vfs, zx_handle_t vmo, zx_off_t length);
    ~VnodeFile();

    virtual zx_status_t ValidateFlags(uint32_t flags) final;

private:
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Write(const void* data, size_t len, size_t offset,
                      size_t* out_actual) final;
    zx_status_t Append(const void* data, size_t len, size_t* out_end,
                       size_t* out_actual) final;
    zx_status_t Truncate(size_t len) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                           zxrio_object_info_t* extra) final;
    zx_status_t GetVmo(int flags, zx_handle_t* out) final;

    zx::vmo vmo_;
    zx_off_t length_;
};

class VnodeDir final : public VnodeMemfs {
public:
    VnodeDir(Vfs* vfs);
    virtual ~VnodeDir();

    virtual zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) final;

    // Create a vnode from a VMO.
    // Fails if the vnode already exists.
    // Passes the vmo to the Vnode; does not duplicate it.
    zx_status_t CreateFromVmo(bool vmofile, fbl::StringPiece name, zx_handle_t vmo,
                              zx_off_t off, zx_off_t len);

    // Mount a subtree as a child of this directory.
    void MountSubtree(fbl::RefPtr<VnodeDir> subtree);

    // Use the watcher container to implement a directory watcher
    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) final;

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const final;
    virtual zx::channel DetachRemote() final;
    virtual zx_handle_t GetRemote() const final;
    virtual void SetRemote(zx::channel remote) final;

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
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                           zxrio_object_info_t* extra) final;
    zx_status_t GetVmo(int flags,  zx_handle_t* out) final;

    fs::RemoteContainer remoter_;
    fs::WatcherContainer watcher_;
};

class VnodeVmo final : public VnodeMemfs {
public:
    VnodeVmo(Vfs* vfs, zx_handle_t vmo, zx_off_t offset, zx_off_t length);
    ~VnodeVmo();

    virtual zx_status_t ValidateFlags(uint32_t flags) override;

private:
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                           zxrio_object_info_t* extra) final;

    zx_handle_t vmo_;
    zx_off_t offset_;
    zx_off_t length_;
    bool have_local_clone_;
};

class Vfs : public fs::ManagedVfs {
public:
    zx_status_t CreateFromVmo(VnodeDir* parent, bool vmofile, fbl::StringPiece name,
                              zx_handle_t vmo, zx_off_t off,
                              zx_off_t len);

    void MountSubtree(VnodeDir* parent, fbl::RefPtr<VnodeDir> subtree);
};

zx_status_t createFilesystem(const char* name, memfs::Vfs* vfs, fbl::RefPtr<VnodeDir>* out);

} // namespace memfs

#endif  // ifdef __cplusplus
