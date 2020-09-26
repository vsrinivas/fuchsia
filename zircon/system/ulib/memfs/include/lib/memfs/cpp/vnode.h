// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEMFS_CPP_VNODE_H_
#define LIB_MEMFS_CPP_VNODE_H_

#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <atomic>
#include <ctime>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fs/managed_vfs.h>
#include <fs/remote_container.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>
#include <fs/watcher.h>

namespace memfs {

constexpr uint64_t kMemfsBlksize = PAGE_SIZE;

class Dnode;
class Vfs;

class VnodeMemfs : public fs::Vnode {
 public:
  virtual zx_status_t SetAttributes(fs::VnodeAttributesUpdate a) final;
  virtual void Sync(SyncCallback closure) final;
  zx_status_t AttachRemote(fs::MountChannel h) final;

  // To be more specific: Is this vnode connected into the directory hierarchy?
  // VnodeDirs can be unlinked, and this method will subsequently return false.
  bool IsDirectory() const { return dnode_ != nullptr; }
  void UpdateModified() {
    std::timespec ts;
    if (std::timespec_get(&ts, TIME_UTC)) {
      modify_time_ = zx_time_from_timespec(ts);
    } else {
      modify_time_ = 0;
    }
  }

  ~VnodeMemfs() override;

  Vfs* vfs() const { return vfs_; }
  uint64_t ino() const { return ino_; }

  // TODO(smklein): Move member into the VnodeDir subclass.
  // Directories contain a raw reference to their location in the filesystem hierarchy.
  // Although this would have safer memory semantics with an actual weak pointer, it is
  // currently raw to avoid circular dependencies from Vnode -> Dnode -> Vnode.
  //
  // Caution must be taken when detaching Dnodes from their parents to avoid leaving
  // this reference dangling.
  Dnode* dnode_;
  uint32_t link_count_;

 protected:
  explicit VnodeMemfs(Vfs* vfs);

  Vfs* vfs_;
  uint64_t ino_;
  uint64_t create_time_;
  uint64_t modify_time_;

  uint64_t GetInoCounter() const { return ino_ctr_.load(std::memory_order_relaxed); }

  uint64_t GetDeletedInoCounter() const { return deleted_ino_ctr_.load(std::memory_order_relaxed); }

 private:
  static std::atomic<uint64_t> ino_ctr_;
  static std::atomic<uint64_t> deleted_ino_ctr_;
};

class VnodeFile final : public VnodeMemfs {
 public:
  explicit VnodeFile(Vfs* vfs);
  ~VnodeFile() override;

  fs::VnodeProtocolSet GetProtocols() const final;

 private:
  zx_status_t CreateStream(uint32_t stream_options, zx::stream* out_stream) final;
  void DidModifyStream() final;

  zx_status_t Truncate(size_t len) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) final;

  zx_status_t CreateBackingStoreIfNeeded();
  size_t GetContentSize() const;

  // Ensure the underlying vmo is filled with zero from:
  // [start, round_up(end, PAGE_SIZE)).
  void ZeroTail(size_t start, size_t end);

  zx::vmo vmo_;
};

class VnodeDir final : public VnodeMemfs {
 public:
  explicit VnodeDir(Vfs* vfs);
  ~VnodeDir() override;

  fs::VnodeProtocolSet GetProtocols() const final;

  zx_status_t Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t Create(fbl::StringPiece name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;

  // Create a vnode from a VMO.
  // Fails if the vnode already exists.
  // Passes the vmo to the Vnode; does not duplicate it.
  zx_status_t CreateFromVmo(fbl::StringPiece name, zx_handle_t vmo, zx_off_t off, zx_off_t len);

  // Use the watcher container to implement a directory watcher
  void Notify(fbl::StringPiece name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;
  zx_status_t QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* out) final;

  // Vnode overrides.
  //
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
  zx_status_t AttachVnode(fbl::RefPtr<memfs::VnodeMemfs> vn, fbl::StringPiece name, bool isdir);

  zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, fbl::StringPiece oldname,
                     fbl::StringPiece newname, bool src_must_be_dir, bool dst_must_be_dir) final;
  zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) final;

  fs::RemoteContainer remoter_;
  fs::WatcherContainer watcher_;
};

class VnodeVmo final : public VnodeMemfs {
 public:
  VnodeVmo(Vfs* vfs, zx_handle_t vmo, zx_off_t offset, zx_off_t length);
  ~VnodeVmo() override;

  fs::VnodeProtocolSet GetProtocols() const final;
  bool ValidateRights(fs::Rights rights) final;

 private:
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) final;
  zx_status_t MakeLocalClone();

  zx_handle_t vmo_;
  zx_off_t offset_;
  zx_off_t length_;
  bool executable_;
  bool have_local_clone_;
};

class Vfs : public fs::ManagedVfs {
 public:
  static zx_status_t Create(const char* fs_name, std::unique_ptr<Vfs>* out_vfs,
                            fbl::RefPtr<VnodeDir>* out_root);

  ~Vfs();

  // Creates a VnodeVmo under |parent| with |name| which is backed by |vmo|.
  // N.B. The VMO will not be taken into account when calculating
  // number of allocated pages in this Vfs.
  zx_status_t CreateFromVmo(VnodeDir* parent, fbl::StringPiece name, zx_handle_t vmo, zx_off_t off,
                            zx_off_t len);

  uint64_t GetFsId() const { return fs_id_; }

  // Increases the size of the |vmo| to at least |request_size| bytes.
  // If the VMO is invalid, it will try to create it.
  // |current_size| is the current size of the VMO in number of bytes. It should be
  // a multiple of page size. The new size of the VMO is returned via |actual_size|.
  // If the new size would cause us to exceed the limit on number of pages or if the system
  // ran out of memory, an error is returned.
  zx_status_t GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size, size_t* actual_size);

 private:
  explicit Vfs(uint64_t id, const char* name);

  uint64_t fs_id_ = 0;

  // Since no directory contains the root, it is owned by the VFS object.
  std::unique_ptr<Dnode> root_;
};

}  // namespace memfs

#endif  // LIB_MEMFS_CPP_VNODE_H_
