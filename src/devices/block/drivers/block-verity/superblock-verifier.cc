// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/superblock-verifier.h"

#include <endian.h>
#include <zircon/status.h>

#include <array>

#include <ddk/debug.h>
#include <digest/digest.h>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/superblock.h"

namespace block_verity {
namespace {

static void ReadCompletedCallback(void* cookie, zx_status_t status, block_op_t* block) {
  // Static trampoline to OnReadCompleted.
  SuperblockVerifier* verifier = static_cast<SuperblockVerifier*>(cookie);
  verifier->OnReadCompleted(status, block);
}

}  // namespace

SuperblockVerifier::SuperblockVerifier(
    DeviceInfo info, const std::array<uint8_t, kHashOutputSize>& expected_superblock_hash)
    : info_(std::move(info)), expected_superblock_hash_(expected_superblock_hash) {
  block_op_buf_ = std::make_unique<uint8_t[]>(info_.upstream_op_size);
}

zx_status_t SuperblockVerifier::StartVerifying(void* cookie, SuperblockVerifierCallback callback) {
  zx_status_t rc;
  if ((rc = zx::vmo::create(kBlockSize, 0, &block_op_vmo_)) != ZX_OK) {
    zxlogf(ERROR, "zx::vmo::create failed: %s", zx_status_get_string(rc));
    return rc;
  }

  // Save the callback & userdata.
  cookie_ = cookie;
  callback_ = callback;

  // Request to read the superblock.

  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->rw.command = BLOCK_OP_READ;
  block_op->rw.length = info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_dev = 0;  // Superblock is always block 0
  block_op->rw.offset_vmo = 0;  // Write to start of VMO
  block_op->rw.vmo = block_op_vmo_.get();

  // Send read request.
  info_.block_protocol.Queue(block_op, ReadCompletedCallback, this);
  return ZX_OK;
}

void SuperblockVerifier::OnReadCompleted(zx_status_t status, block_op_t* block) {
  // Check status.
  if (status != ZX_OK) {
    callback_(cookie_, status, nullptr);
    return;
  }

  // Read the block from the VMO into the buffer.  For a one-time 4k read, it's
  // not worth mapping the VMO.
  zx_status_t rc = block_op_vmo_.read(&superblock_, 0, kBlockSize);
  if (rc != ZX_OK) {
    callback_(cookie_, rc, nullptr);
    return;
  }

  // Hash the block.  Compare against expected hash.  If no match, complete
  // with a data integrity error.
  digest::Digest hasher;
  const uint8_t* block_hash = hasher.Hash(&superblock_, kBlockSize);
  if (memcmp(block_hash, expected_superblock_hash_.data(), kHashOutputSize) != 0) {
    callback_(cookie_, ZX_ERR_IO_DATA_INTEGRITY, nullptr);
    return;
  }

  // Check that the contents of the block are well-understood, and match
  // the expected values.
  uint64_t block_count = le64toh(superblock_.block_count);
  if ((memcmp(superblock_.magic, kBlockVerityMagic, sizeof(kBlockVerityMagic)) != 0) ||
      (block_count != info_.geometry.total_blocks_) || (block_count > kMaxBlockCount) ||
      (le32toh(superblock_.block_size) != kBlockSize) ||
      (le32toh(superblock_.hash_function) != kSHA256HashTag)) {
    callback_(cookie_, ZX_ERR_INVALID_ARGS, nullptr);
    return;
  }

  callback_(cookie_, ZX_OK, &superblock_);
}

}  // namespace block_verity
