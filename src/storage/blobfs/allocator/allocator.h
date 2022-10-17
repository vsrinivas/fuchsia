// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_
#define SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_

#include <fidl/fuchsia.blobfs/cpp/wire.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <vector>

#include <bitmap/raw-bitmap.h>
#include <id_allocator/id_allocator.h>
#include <storage/buffer/vmoid_registry.h>

#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"
#include "src/storage/blobfs/allocator/base_allocator.h"
#include "src/storage/blobfs/allocator/extent_reserver.h"
#include "src/storage/blobfs/allocator/node_reserver.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

class Allocator;

// An interface which controls actual access to the underlying storage.
class SpaceManager : public storage::VmoidRegistry {
 public:
  ~SpaceManager() override = default;

  virtual const Superblock& Info() const = 0;

  // Adds any number of nodes to |allocator|'s node map, extending the volume if necessary.
  virtual zx_status_t AddInodes(Allocator* allocator) = 0;

  // Adds space for |nblocks| blocks to |map|, extending the volume if necessary.
  virtual zx_status_t AddBlocks(uint64_t nblocks, RawBitmap* map) = 0;
};

// Allocates and frees both block and node entries.
//
// Also maintains reservation mappings, to help in-progress allocations avoid from being persisted
// too early.
class Allocator : public BaseAllocator {
 public:
  Allocator(SpaceManager* space_manager, RawBitmap block_map, fzl::ResizeableVmoMapper node_map,
            std::unique_ptr<id_allocator::IdAllocator> node_bitmap);
  ~Allocator() override = default;

  // blobfs::NodeFinder interface.
  zx::result<InodePtr> GetNode(uint32_t node_index) final;

  void SetLogging(bool enable) { log_allocation_failure_ = enable; }

  // Reads the block map and node map from underlying storage, using a blocking read transaction.
  //
  // It is unsafe to call this method while any nodes or blocks are reserved.
  zx_status_t ResetFromStorage(fs::DeviceTransactionHandler& transaction_handler);

  // Provides a read-only view into the block map.
  const zx::vmo& GetBlockMapVmo() const;

  // Provides a read-only view into the node map.
  const zx::vmo& GetNodeMapVmo() const;

  // blobfs::NodeReserverInterface interface.
  zx::result<ReservedNode> ReserveNode() final;
  // Called when InodePtr goes out of scope.
  void DropInodePtr() final;

  // Grows node map to |size|. The caller takes responsibility for initializing the new entries.
  [[nodiscard]] zx_status_t GrowNodeMap(size_t size);

 protected:
  // blobfs::BaseAllocator interface.
  zx::result<> AddBlocks(uint64_t block_count) final;
  zx::result<> AddNodes() final;

 private:
  // Resets the size of the block map based on |Info().data_block_count|.
  //
  // It is unsafe to call this method while any blocks are reserved.
  zx_status_t ResetBlockMapSize();

  // Resets the size of the node map based on |Info().inode_count|.
  //
  // It is unsafe to call this method while any nodes are reserved.
  zx_status_t ResetNodeMapSize();

  void LogAllocationFailure(uint64_t num_blocks) const;

  SpaceManager* space_manager_;

  fzl::ResizeableVmoMapper node_map_;
  // Guards growing node_map_, which will invalidate outstanding pointers.
  std::shared_mutex node_map_grow_mutex_;

  bool log_allocation_failure_ = true;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ALLOCATOR_ALLOCATOR_H_
