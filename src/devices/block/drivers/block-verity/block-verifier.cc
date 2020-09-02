// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/block-verifier.h"

#include <lib/zx/vmar.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <digest/digest.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "src/devices/block/drivers/block-verity/block-loader-interface.h"
#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/geometry.h"

namespace block_verity {
namespace {

static void IntegrityBlockCallback(void* cookie, zx_status_t status) {
  BlockVerifier* verifier = static_cast<BlockVerifier*>(cookie);
  verifier->OnIntegrityDataLoaded(status);
}

}  // namespace

BlockVerifier::BlockVerifier(const Geometry& geometry,
                             const std::array<uint8_t, kHashOutputSize>& integrity_root_hash,
                             BlockLoaderInterface* block_loader)
    : block_loader_(block_loader),
      geometry_(geometry),
      state_(kInitial),
      root_hash_(integrity_root_hash) {}

BlockVerifier::~BlockVerifier() {
  // Unmap the vmo from the vmar.
  if (integrity_block_base_ == nullptr) {
    return;
  }
  uintptr_t address = reinterpret_cast<uintptr_t>(integrity_block_base_);
  integrity_block_base_ = nullptr;
  // Ignore unmap failures; we're destructing anyway
  zx::vmar::root_self()->unmap(address, GetIntegritySectionSizeInBytes());
}

zx_status_t BlockVerifier::PrepareAsync(void* cookie, BlockVerifierCallback callback) {
  {
    // Scoped so we don't hold mtx_ while calling LoadIntegrityBlocks.
    fbl::AutoLock lock(&mtx_);

    ZX_ASSERT(state_ == kInitial);

    // Allocate and map a VMO for block operations
    zx_status_t rc;
    if ((rc = zx::vmo::create(GetIntegritySectionSizeInBytes(), 0, &integrity_block_vmo_)) !=
        ZX_OK) {
      return rc;
    }
    auto cleanup = fbl::MakeAutoCall([this]() { integrity_block_vmo_.reset(); });
    constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
    uintptr_t address;
    if ((rc = zx::vmar::root_self()->map(0, integrity_block_vmo_, 0,
                                         GetIntegritySectionSizeInBytes(), flags, &address)) !=
        ZX_OK) {
      return rc;
    }
    integrity_block_base_ = reinterpret_cast<const uint8_t*>(address);
    cleanup.cancel();

    cookie_ = cookie;
    callback_ = callback;
    state_ = kLoading;
  }
  LoadIntegrityBlocks();

  return ZX_OK;
}

void BlockVerifier::LoadIntegrityBlocks() {
  uint64_t integrity_start_block = geometry_.AbsoluteLocationForIntegrity(0);
  uint64_t integrity_block_count = geometry_.allocation_.integrity_shape.integrity_block_count;
  block_loader_->RequestBlocks(integrity_start_block, integrity_block_count, integrity_block_vmo_,
                               this, IntegrityBlockCallback);
}

void BlockVerifier::OnIntegrityDataLoaded(zx_status_t status) {
  {
    fbl::AutoLock lock(&mtx_);
    ZX_ASSERT(state_ == kLoading);
    if (status == ZX_OK) {
      state_ = kReady;
    } else {
      state_ = kFailed;
    }
  }

  callback_(cookie_, status);
}

uint64_t BlockVerifier::GetIntegritySectionSizeInBytes() const {
  return geometry_.allocation_.integrity_shape.integrity_block_count * kBlockSize;
}

const uint8_t* BlockVerifier::MemoryLocationForHash(HashLocation h) const {
  uint64_t offset = (h.integrity_block * kBlockSize) + (h.hash_in_block * kHashOutputSize);
  return integrity_block_base_ + offset;
}

const uint8_t* BlockVerifier::MemoryLocationForBlock(IntegrityBlockIndex i) const {
  uint64_t offset = i * kBlockSize;
  return integrity_block_base_ + offset;
}

zx_status_t BlockVerifier::VerifyDataBlockSync(uint64_t data_block_index,
                                               const uint8_t* block_data) {
  {
    // Since `kReady` is a terminal state, we can release the lock as soon as
    // we're done checking state.
    fbl::AutoLock lock(&mtx_);
    if (state_ != kReady) {
      return ZX_ERR_BAD_STATE;
    }
  }

  digest::Digest hasher;
  hasher.Hash(block_data, kBlockSize);

  // Check that the data block matches the hash in the leaf integrity block.
  HashLocation leaf_hash_location = geometry_.IntegrityDataLocationForDataBlock(data_block_index);
  const uint8_t* leaf_integrity_range = MemoryLocationForHash(leaf_hash_location);
  if (!hasher.Equals(leaf_integrity_range, kHashOutputSize)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Future performance improvement: make this cache successfully-hashed
  // indirect integrity blocks rather than rehashing to the root every time.
  uint32_t distance_from_leaf = 0;
  HashLocation previous = leaf_hash_location;
  while (distance_from_leaf < geometry_.allocation_.integrity_shape.tree_depth - 1) {
    // Get address of containing block.  Hash it.
    const uint8_t* containing_block = MemoryLocationForBlock(previous.integrity_block);
    hasher.Hash(containing_block, kBlockSize);

    HashLocation up_one =
        geometry_.NextIntegrityBlockUp(distance_from_leaf, previous.integrity_block);
    if (!hasher.Equals(MemoryLocationForHash(up_one), kHashOutputSize)) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    previous = up_one;
    distance_from_leaf++;
  }

  // Validate the root hash.  By now the last integrity range we checked
  // should have been in the final integrity block, which is the root integrity
  // block.
  ZX_ASSERT(previous.integrity_block ==
            geometry_.allocation_.integrity_shape.integrity_block_count - 1);

  const uint8_t* root_integrity_block = MemoryLocationForBlock(previous.integrity_block);
  hasher.Hash(root_integrity_block, kBlockSize);
  if (!hasher.Equals(root_hash_.data(), kHashOutputSize)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  return ZX_OK;
}

}  // namespace block_verity
