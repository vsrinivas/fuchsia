// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_FACTORYFS_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_FACTORYFS_H_

#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/status.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <block-client/cpp/remote-block-device.h>
#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <storage/buffer/vmoid_registry.h>

#include "src/storage/factory/factoryfs/directory.h"
#include "src/storage/factory/factoryfs/format.h"
#include "src/storage/factory/factoryfs/mount.h"

namespace factoryfs {

using ::block_client::BlockDevice;

uint32_t FsToDeviceBlocks(uint32_t fs_block, uint32_t disk_block);

class Factoryfs {
 public:
  Factoryfs(const Factoryfs&) = delete;
  Factoryfs(Factoryfs&&) = delete;
  Factoryfs& operator=(const Factoryfs&) = delete;
  Factoryfs& operator=(Factoryfs&&) = delete;

  // Creates a Factoryfs object.
  //
  // The dispatcher should be for the current thread that Factoryfs is running on.
  static zx_status_t Create(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                            MountOptions* options, std::unique_ptr<Factoryfs>* out);

  virtual ~Factoryfs();
  zx_status_t OpenRootNode(fbl::RefPtr<fs::Vnode>* out);

  const Superblock& Info() const { return superblock_; }

  // Returns the dispatcher for the current thread that factoryfs uses.
  // async_dispatcher_t& dispatcher() { return dispatcher_; }

  BlockDevice& Device() const { return *block_device_; }
  const fuchsia_hardware_block_BlockInfo& GetDeviceBlockInfo() const { return block_info_; }

  // Returns an unique identifier for this instance. Each invocation returns a new
  // handle that references the same kernel object, which is used as the identifier.
  zx_status_t GetFsId(zx::event* out_fs_id) const;
  uint64_t GetFsIdLegacy() const { return fs_id_legacy_; }

  // Returns a vnode for a given path.
  zx::status<fbl::RefPtr<fs::Vnode>> Lookup(std::string_view path);

  // Called when a vnode is opened.
  void DidOpen(std::string_view path, fs::Vnode& vnode);

  // Called when a vnode is closed.
  void DidClose(std::string_view path);

 private:
  // The callback is used by ParseEntries.  If a callback returns ZX_OK, the
  // iteration stops.
  using Callback = fbl::Function<zx_status_t(const DirectoryEntry* entry)>;

  Factoryfs(std::unique_ptr<BlockDevice> device, const Superblock* info);

  uint64_t GetDirectorySize() const {
    return superblock_.directory_ent_blocks * kFactoryfsBlockSize;
  }

  zx_status_t InitDirectoryVmo();

  // Parses all entries in the container directory from offset 0.
  // |parse_data| is guarenteed to be 4 byte aligned.
  zx_status_t ParseEntries(Callback callback, void* parse_data);

  // Returns a DirectoryEntryManager for the given path.
  zx::status<std::unique_ptr<DirectoryEntryManager>> LookupInternal(std::string_view path);

  // Terminates all internal connections and returns the underlying
  // block device.
  std::unique_ptr<BlockDevice> Reset();

  // Dispatcher for the thread this object is running on.
  // async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<BlockDevice> block_device_;
  Superblock superblock_;
  fuchsia_hardware_block_BlockInfo block_info_ = {};
  uint64_t fs_id_legacy_ = 0;
  // A unique identifier for this filesystem instance.
  zx::event fs_id_;

  // VMO used for the directory entries.
  zx::vmo directory_vmo_;

  // Caches open vnodes.  These are unowned pointers so vnodes need to ensure they remove themselves
  // when destroyed.
  std::map<std::string, fs::Vnode*, std::less<>> open_vnodes_cache_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_FACTORYFS_H_
