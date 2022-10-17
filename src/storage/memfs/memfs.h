// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_MEMFS_H_
#define SRC_STORAGE_MEMFS_MEMFS_H_

#include <zircon/types.h>

#include <string_view>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/watcher.h"

namespace memfs {

class Dnode;
class VnodeDir;

// Returns the page size used by Memfs (this is just the system memory page size).
uint64_t GetPageSize();

class Memfs : public fs::ManagedVfs {
 public:
  static zx_status_t Create(async_dispatcher_t* dispatcher, std::string_view fs_name,
                            std::unique_ptr<Memfs>* out_vfs, fbl::RefPtr<VnodeDir>* out_root);

  struct Options {
    uint64_t max_file_size = uint64_t{512} * 1024 * 1024;
  };
  static zx_status_t CreateWithOptions(async_dispatcher_t* dispatcher, std::string_view fs_name,
                                       Options options, std::unique_ptr<Memfs>* out_vfs,
                                       fbl::RefPtr<VnodeDir>* out_root);

  ~Memfs();

  // Creates a VnodeVmo under |parent| with |name| which is backed by |vmo|.
  // N.B. The VMO will not be taken into account when calculating
  // number of allocated pages in this Memfs.
  zx_status_t CreateFromVmo(VnodeDir* parent, std::string_view name, zx_handle_t vmo, zx_off_t off,
                            zx_off_t len);

  // Increases the size of the |vmo| to at least |request_size| bytes.
  // If the VMO is invalid, it will try to create it.
  // |current_size| is the current size of the VMO in number of bytes. It should be
  // a multiple of page size. The new size of the VMO is returned via |actual_size|.
  // If the new size would cause us to exceed the limit on number of pages or if the system
  // ran out of memory, an error is returned.
  zx_status_t GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size, size_t* actual_size);

  // fs::FuchsiaVfs override:
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() override;

 private:
  explicit Memfs(async_dispatcher_t* dispatcher);

  // This event's koid is used as a unique identifier for this filesystem instance.
  zx::event fs_id_;

  // Since no directory contains the root, it is owned by the VFS object.
  std::unique_ptr<Dnode> root_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_MEMFS_H_
