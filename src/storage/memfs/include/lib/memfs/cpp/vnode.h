// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_
#define SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <atomic>
#include <ctime>
#include <string_view>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/remote_container.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/lib/storage/vfs/cpp/watcher.h"

namespace memfs {

class Dnode;
class Vfs;

// Returns the page size used by Memfs (this is just the system memory page size).
uint64_t GetPageSize();

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
  Dnode* dnode_ = nullptr;
  uint32_t link_count_ = 0;

 protected:
  explicit VnodeMemfs(Vfs* vfs);

  Vfs* vfs_ = nullptr;
  uint64_t ino_ = 0;
  uint64_t create_time_ = 0;
  uint64_t modify_time_ = 0;

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

  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;

  // Create a vnode from a VMO.
  // Fails if the vnode already exists.
  // Passes the vmo to the Vnode; does not duplicate it.
  zx_status_t CreateFromVmo(std::string_view name, zx_handle_t vmo, zx_off_t off, zx_off_t len);

  // Use the watcher container to implement a directory watcher
  void Notify(std::string_view name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;
  zx_status_t QueryFilesystem(fuchsia_io::wire::FilesystemInfo* out) final;

  // Vnode overrides.
  //
  // The vnode is acting as a mount point for a remote filesystem or device.
  bool IsRemote() const final;
  fidl::ClientEnd<fuchsia_io::Directory> DetachRemote() final;
  fidl::UnownedClientEnd<fuchsia_io::Directory> GetRemote() const final;
  void SetRemote(fidl::ClientEnd<fuchsia_io::Directory> remote) final;

 private:
  zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) final;

  // Resolves the question, "Can this directory create a child node with the name?"
  // Returns "ZX_OK" on success; otherwise explains failure with error message.
  zx_status_t CanCreate(std::string_view name) const;

  // Creates a dnode for the Vnode, attaches vnode to dnode, (if directory) attaches
  // dnode to vnode, and adds dnode to parent directory.
  zx_status_t AttachVnode(fbl::RefPtr<memfs::VnodeMemfs> vn, std::string_view name, bool isdir);

  zx_status_t Unlink(std::string_view name, bool must_be_dir) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, std::string_view oldname,
                     std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) final;
  zx_status_t Link(std::string_view name, fbl::RefPtr<fs::Vnode> target) final;
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

  zx_handle_t vmo_ = ZX_HANDLE_INVALID;
  zx_off_t offset_ = 0;
  zx_off_t length_ = 0;
  bool executable_ = false;
  bool have_local_clone_ = false;
};

class Vfs : public fs::ManagedVfs {
 public:
  static zx_status_t Create(async_dispatcher_t* dispatcher, std::string_view fs_name,
                            std::unique_ptr<Vfs>* out_vfs, fbl::RefPtr<VnodeDir>* out_root);

  ~Vfs();

  // Creates a VnodeVmo under |parent| with |name| which is backed by |vmo|.
  // N.B. The VMO will not be taken into account when calculating
  // number of allocated pages in this Vfs.
  zx_status_t CreateFromVmo(VnodeDir* parent, std::string_view name, zx_handle_t vmo, zx_off_t off,
                            zx_off_t len);

  uint64_t GetFsId() const;

  // Increases the size of the |vmo| to at least |request_size| bytes.
  // If the VMO is invalid, it will try to create it.
  // |current_size| is the current size of the VMO in number of bytes. It should be
  // a multiple of page size. The new size of the VMO is returned via |actual_size|.
  // If the new size would cause us to exceed the limit on number of pages or if the system
  // ran out of memory, an error is returned.
  zx_status_t GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size, size_t* actual_size);

 private:
  explicit Vfs(async_dispatcher_t* dispatcher);

  // This event's koid is used as a unique identifier for this filesystem instance. This must be
  // an event because it's returned by the fs.Query interface.
  zx::event fs_id_;

  // Since no directory contains the root, it is owned by the VFS object.
  std::unique_ptr<Dnode> root_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_
