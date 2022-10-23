// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/host_allocator.h"

#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <bitmap/raw-bitmap.h>
#include <id_allocator/id_allocator.h>
#include <safemath/safe_conversions.h>

#include "src/storage/blobfs/allocator/base_allocator.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

HostAllocator::HostAllocator(RawBitmap block_bitmap, cpp20::span<Inode> node_map,
                             std::unique_ptr<id_allocator::IdAllocator> node_bitmap)
    : BaseAllocator(std::move(block_bitmap), std::move(node_bitmap)), node_map_(node_map) {}

zx::result<std::unique_ptr<HostAllocator>> HostAllocator::Create(RawBitmap block_bitmap,
                                                                 cpp20::span<Inode> node_map) {
  std::unique_ptr<id_allocator::IdAllocator> node_bitmap;
  if (zx_status_t status = id_allocator::IdAllocator::Create(node_map.size(), &node_bitmap);
      status != ZX_OK) {
    return zx::error(status);
  }

  auto host_allocator = std::unique_ptr<HostAllocator>(
      new HostAllocator(std::move(block_bitmap), node_map, std::move(node_bitmap)));

  // Node indices are only 32 bits in Blobfs.
  const uint32_t node_map_size = safemath::checked_cast<uint32_t>(node_map.size());

  for (uint32_t i = 0; i < node_map_size; ++i) {
    if (node_map[i].header.IsAllocated()) {
      host_allocator->MarkNodeAllocated(i);
    }
  }

  return zx::ok(std::move(host_allocator));
}

zx::result<InodePtr> HostAllocator::GetNode(uint32_t node_index) {
  if (node_index >= node_map_.size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(InodePtr(&node_map_[node_index], InodePtrDeleter(nullptr)));
}

void* HostAllocator::GetBlockBitmapData() { return GetBlockBitmap().StorageUnsafe()->GetData(); }

}  // namespace blobfs
