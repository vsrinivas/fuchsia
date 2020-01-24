// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <memory>

#include <fbl/auto_call.h>
#include <safemath/checked_math.h>
#include <zxtest/zxtest.h>

using id_allocator::IdAllocator;

namespace blobfs {

namespace {

void DetachVmo(BlockDevice* device, vmoid_t id) {
  block_fifo_request_t request;
  request.opcode = BLOCKIO_CLOSE_VMO;
  request.vmoid = id;
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  ASSERT_OK(device->FifoTransaction(&request, 1));
}

void AttachVmo(BlockDevice* device, zx::vmo* vmo, vmoid_t* out_id) {
  fuchsia_hardware_block_VmoId vmoid;

  ASSERT_OK(device->BlockAttachVmo(*vmo, &vmoid));

  *out_id = vmoid.id;
}

// Verify that the |size| and |offset| are |device| block size aligned.
// Returns block_size in |out_block_size|.
void VerifySizeBlockAligned(BlockDevice* device, size_t size, uint64_t offset,
                            uint32_t* out_block_size) {
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_OK(device->BlockGetInfo(&info));
  ASSERT_EQ(size % info.block_size, 0);
  ASSERT_EQ(offset % info.block_size, 0);
  *out_block_size = info.block_size;
}

}  // namespace

zx_status_t MockTransactionManager::Transaction(block_fifo_request_t* requests, size_t count) {
  fbl::AutoLock lock(&lock_);

  if (transaction_callback_) {
    for (size_t i = 0; i < count; i++) {
      if (attached_vmos_.size() < requests[i].vmoid) {
        return ZX_ERR_INVALID_ARGS;
      }

      std::optional<zx::vmo>* optional_vmo = &attached_vmos_[requests[i].vmoid - 1];

      if (!optional_vmo->has_value()) {
        return ZX_ERR_BAD_STATE;
      }

      const zx::vmo& dest_vmo = optional_vmo->value();

      if (dest_vmo.get() == ZX_HANDLE_INVALID) {
        return ZX_ERR_INVALID_ARGS;
      }

      zx_status_t status = transaction_callback_(requests[i], dest_vmo);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  return ZX_OK;
}

zx_status_t MockTransactionManager::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
  fbl::AutoLock lock(&lock_);
  zx::vmo duplicate_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_vmo);
  if (status != ZX_OK) {
    return status;
  }
  attached_vmos_.push_back(std::move(duplicate_vmo));
  *out = static_cast<uint16_t>(attached_vmos_.size());
  if (*out == 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

zx_status_t MockTransactionManager::DetachVmo(vmoid_t vmoid) {
  fbl::AutoLock lock(&lock_);
  if (attached_vmos_.size() < vmoid) {
    return ZX_ERR_INVALID_ARGS;
  }

  attached_vmos_[vmoid - 1].reset();
  return ZX_OK;
}

// Create a block and node map of the requested size, update the superblock of
// the |space_manager|, and create an allocator from this provided info.
void InitializeAllocator(size_t blocks, size_t nodes, MockSpaceManager* space_manager,
                         std::unique_ptr<Allocator>* out) {
  RawBitmap block_map;
  ASSERT_OK(block_map.Reset(blocks));
  fzl::ResizeableVmoMapper node_map;
  ASSERT_OK(node_map.CreateAndMap(nodes * kBlobfsBlockSize, "node map"));

  space_manager->MutableInfo().inode_count = nodes;
  space_manager->MutableInfo().data_block_count = blocks;
  std::unique_ptr<IdAllocator> nodes_bitmap = {};
  ASSERT_OK(IdAllocator::Create(nodes, &nodes_bitmap), "nodes bitmap");
  *out = std::make_unique<Allocator>(space_manager, std::move(block_map), std::move(node_map),
                                     std::move(nodes_bitmap));
  (*out)->SetLogging(false);
}

// Force the allocator to become maximally fragmented by allocating
// every-other block within up to |blocks|.
void ForceFragmentation(Allocator* allocator, size_t blocks) {
  fbl::Vector<ReservedExtent> extents[blocks];
  for (size_t i = 0; i < blocks; i++) {
    ASSERT_OK(allocator->ReserveBlocks(1, &extents[i]));
    ASSERT_EQ(1, extents[i].size());
  }

  for (size_t i = 0; i < blocks; i += 2) {
    allocator->MarkBlocksAllocated(extents[i][0]);
  }
}

// Save the extents within |in| in a non-reserved vector |out|.
void CopyExtents(const fbl::Vector<ReservedExtent>& in, fbl::Vector<Extent>* out) {
  out->reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    out->push_back(in[i].extent());
  }
}

// Save the nodes within |in| in a non-reserved vector |out|.
void CopyNodes(const fbl::Vector<ReservedNode>& in, fbl::Vector<uint32_t>* out) {
  out->reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    out->push_back(in[i].index());
  }
}

void DeviceBlockRead(BlockDevice* device, void* buf, size_t size, uint64_t dev_offset) {
  uint32_t block_size;
  VerifySizeBlockAligned(device, size, dev_offset, &block_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));

  vmoid_t vmoid;
  AttachVmo(device, &vmo, &vmoid);

  auto cleanup = fbl::MakeAutoCall([device, vmoid]() { DetachVmo(device, vmoid); });

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid;
  request.length = safemath::checked_cast<uint32_t>(size / block_size);
  request.vmo_offset = 0;
  request.dev_offset = safemath::checked_cast<uint32_t>(dev_offset / block_size);
  ASSERT_OK(device->FifoTransaction(&request, 1));
  ASSERT_OK(vmo.read(buf, 0, size));
}

void DeviceBlockWrite(BlockDevice* device, const void* buf, size_t size, uint64_t dev_offset) {
  uint32_t block_size;
  VerifySizeBlockAligned(device, size, dev_offset, &block_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));
  ASSERT_OK(vmo.write(buf, 0, size));

  vmoid_t vmoid;
  AttachVmo(device, &vmo, &vmoid);
  auto cleanup = fbl::MakeAutoCall([device, vmoid]() { DetachVmo(device, vmoid); });

  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid;
  request.length = safemath::checked_cast<uint32_t>(size / block_size);
  request.vmo_offset = 0;
  request.dev_offset = safemath::checked_cast<uint32_t>(dev_offset / block_size);
  ASSERT_OK(device->FifoTransaction(&request, 1));
}

}  // namespace blobfs
