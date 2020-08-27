// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_VERIFIER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_VERIFIER_H_

#include <array>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "block-loader-interface.h"
#include "constants.h"
#include "geometry.h"

namespace block_verity {

class BlockLoaderInterface;

typedef void (*BlockVerifierCallback)(void* cookie, zx_status_t status);

// `BlockVerifier` loads the integrity data merkle tree into memory and then can
// be used to detect if any data block read from the device has changed since
// the device was sealed.
//
// Example usage:
//   BlockVerifier verifier(geometry, root_hash, block_loader);
//   verifier.PrepareAsync(this, OnVerifierReady);
//   ... after OnVerifierReady called ...
//   zx_status_t result = verifier.VerifyDataBlockSync(dev_offset, buf);
class BlockVerifier final {
 public:
  // Note: `block_loader` is expected to be caller-owned and must outlive this
  // `BlockVerfifier`
  BlockVerifier(const Geometry& geometry,
                const std::array<uint8_t, kHashOutputSize>& integrity_root_hash,
                BlockLoaderInterface* block_loader);
  // Disallow copy, assign, move
  BlockVerifier(const BlockVerifier&) = delete;
  BlockVerifier(BlockVerifier&&) = delete;
  BlockVerifier& operator=(const BlockVerifier&) = delete;
  BlockVerifier& operator=(BlockVerifier&&) = delete;

  ~BlockVerifier();

  // Make whatever preparations are needed to be able to verify blocks, then
  // trigger callback when done.
  zx_status_t PrepareAsync(void* cookie, BlockVerifierCallback callback) __TA_EXCLUDES(&mtx_);

  // Actually do the hashing to determine if the kBlockSize bytes of data
  // pointed to by block_data correctly represent the contents of data block
  // `block_index`.  In the future, it might make sense to move to async
  // block verification.
  zx_status_t VerifyDataBlockSync(uint64_t data_block_index, const uint8_t* block_data)
      __TA_EXCLUDES(&mtx_);

  // Issue the request to load integrity blocks to `block_loader_`.
  void LoadIntegrityBlocks() __TA_EXCLUDES(&mtx_);

  // Callback used with `LoadIntegrityBlocks`.
  void OnIntegrityDataLoaded(zx_status_t status);

 private:
  // The number of bytes that comprise the entire integrity section.
  uint64_t GetIntegritySectionSizeInBytes() const;

  // Translates a HashLocation into the absolute address at which it can be
  // found mapped in this current address space.
  const uint8_t* MemoryLocationForHash(HashLocation h) const;

  // Translates an IntegrityBlockIndex into the absolute address at which it can
  // be found mapped into the current address space.
  const uint8_t* MemoryLocationForBlock(IntegrityBlockIndex i) const;

  enum BlockVerifierState {
    // State on construction.
    kInitial,

    // State when PrepareAsync is called but not completed
    kLoading,

    // State if PrepareAsync completes successfully.
    kReady,

    // State if PrepareAsync fails, either immediately or asynchronously.
    kFailed,
  };

  // Block I/O abstraction for making this testable.
  BlockLoaderInterface* block_loader_;

  // Device geometry.  Safe to access without the mutex.
  const Geometry geometry_;

  BlockVerifierState state_ __TA_GUARDED(mtx_);

  fbl::Mutex mtx_;

  // Copy of the root hash lent to us at initialization.
  // Stays the same over the lifetime of this instance.
  const std::array<uint8_t, kHashOutputSize> root_hash_;

  // A vmo used to cache all integrity block data, and then mapped at
  // `integrity_block_base_` below.
  zx::vmo integrity_block_vmo_;

  // The start address where that vmo is mapped, at which we can effectively
  // look at all integrity data in a flat array.
  const uint8_t* integrity_block_base_;

  // Args to `PrepareAsync` that we save so we can call them back later,
  // possibly across an async boundary.
  void* cookie_;
  BlockVerifierCallback callback_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_VERIFIER_H_
