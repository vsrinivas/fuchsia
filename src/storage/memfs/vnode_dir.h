// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_VNODE_DIR_H_
#define SRC_STORAGE_MEMFS_VNODE_DIR_H_

#include "src/lib/storage/vfs/cpp/remote_container.h"
#include "src/lib/storage/vfs/cpp/watcher.h"
#include "src/storage/memfs/vnode.h"

namespace memfs {

class VnodeDir final : public Vnode {
 public:
  VnodeDir(PlatformVfs* vfs, uint64_t max_file_size);
  ~VnodeDir() override;

  fs::VnodeProtocolSet GetProtocols() const final;

  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) final;
  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) final;

  // Create a vnode from a VMO.
  // Fails if the vnode already exists.
  // Passes the vmo to the Vnode; does not duplicate it.
  zx_status_t CreateFromVmo(std::string_view name, zx_handle_t vmo, zx_off_t off, zx_off_t len);

  // Use the watcher container to implement a directory watcher
  void Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) final;
  zx_status_t WatchDir(fs::Vfs* vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                       fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) final;

  // Vnode overrides.
  //
  // The vnode is acting as a mount point for a remote filesystem or device.
  bool IsRemote() const final;
  fidl::ClientEnd<fuchsia_io::Directory> DetachRemote() final;
  fidl::UnownedClientEnd<fuchsia_io::Directory> GetRemote() const final;

 private:
  zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) final;

  // Resolves the question, "Can this directory create a child node with the name?"
  // Returns "ZX_OK" on success; otherwise explains failure with error message.
  zx_status_t CanCreate(std::string_view name) const;

  // Creates a dnode for the Vnode, attaches vnode to dnode, (if directory) attaches
  // dnode to vnode, and adds dnode to parent directory.
  zx_status_t AttachVnode(const fbl::RefPtr<memfs::Vnode>& vn, std::string_view name, bool isdir);

  zx_status_t Unlink(std::string_view name, bool must_be_dir) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir, std::string_view oldname,
                     std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) final;
  zx_status_t Link(std::string_view name, fbl::RefPtr<fs::Vnode> target) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) final;
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) final;

  const uint64_t max_file_size_;
  fs::RemoteContainer remoter_;
  fs::WatcherContainer watcher_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_VNODE_DIR_H_
