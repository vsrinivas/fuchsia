// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_VERIFIER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_VERIFIER_H_

#include <lib/zx/vmo.h>

#include <array>
#include <memory>

#include "constants.h"
#include "device-info.h"
#include "superblock.h"

namespace block_verity {

typedef void (*SuperblockVerifierCallback)(void* cookie, zx_status_t status,
                                           const Superblock* superblock);

// `SuperblockVerifier` encapsulates asynchronously loading and verifying that a
// superblock hashes to the `expected_superblock_hash` provided to the
// constructor, and also verifies that the configuration expressed therein is
// supported by this version of the driver.
// Example:
//   SuperblockVerifier verifier(std::move(info), superblock_hash);
//   if (rc != verifier.StartVerifying(this, OnSuperblockVerificationComplete)) {
//     return rc;
//   }
//   /* OnSuperblockVerificationComplete does something useful with the superblock */
class SuperblockVerifier final {
 public:
  SuperblockVerifier(DeviceInfo info,
                     const std::array<uint8_t, kHashOutputSize>& expected_superblock_hash);
  ~SuperblockVerifier() = default;

  // Load the superblock from the device specified in `info_` and verify its hash
  // matches `expected_superblock_hash_`.  If it does, calls `callback` with
  // ZX_OK and a borrowed pointer to a superblock struct in `superblock` which
  // will cease to be valid at the end of the callback.  If not, calls `callback`
  // with a non-ZX_OK status and a null pointer for `superblock`.
  zx_status_t StartVerifying(void* cookie, SuperblockVerifierCallback callback);

  // Callback for underlying async block device I/O.
  void OnReadCompleted(zx_status_t status, block_op_t* block);

 private:
  // Driver geometry/block client handle.
  const DeviceInfo info_;

  // A single block op request buffer, allocated to be the size of the parent
  // block op size request.
  std::unique_ptr<uint8_t[]> block_op_buf_;

  // A vmo used in block device operations.  Don't bother mapping it; it's going
  // to be a single block and zx_vmo_read is faster for small ops.
  zx::vmo block_op_vmo_;

  // Holds the callback function and context pointer across async boundaries.
  // Saved when StartVerifying is called and called exactly once.
  SuperblockVerifierCallback callback_;
  void* cookie_;

  Superblock superblock_;

  // The expected superblock hash
  const std::array<uint8_t, kHashOutputSize> expected_superblock_hash_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_VERIFIER_H_
