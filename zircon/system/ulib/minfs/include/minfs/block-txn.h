// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/algorithm.h>
#include <fbl/macros.h>

#include <fs/block-txn.h>

#include <minfs/bcache.h>
#include <minfs/format.h>

namespace minfs {

#ifdef __Fuchsia__

struct WriteRequest {
  zx_handle_t vmo;
  blk_t vmo_offset;
  blk_t dev_offset;
  blk_t length;
};

// A transaction consisting of enqueued VMOs to be written
// out to disk at specified locations.
//
// TODO(smklein): Rename to LogWriteTxn, or something similar, to imply
// that this write transaction acts fundamentally different from the
// ulib/fs WriteTxn under the hood.
class WriteTxn {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(WriteTxn);
  explicit WriteTxn(Bcache* bc)
      : bc_(bc), vmoid_(fuchsia_hardware_block_VmoID{.id = VMOID_INVALID}), block_count_(0) {}
  ~WriteTxn() { ZX_DEBUG_ASSERT_MSG(requests_.is_empty(), "WriteTxn still has pending requests"); }

  // Identify that a block should be written to disk at a later point in time.
  void Enqueue(zx_handle_t vmo, blk_t vmo_offset, blk_t dev_offset, blk_t nblocks);

  fbl::Vector<WriteRequest>& Requests() { return requests_; }

  // Returns the first block at which this WriteTxn exists within its VMO buffer. Requires all
  // requests within the transaction to have been copied to a single buffer.
  blk_t BlockStart() const;

  // Returns the total number of blocks in all requests within the WriteTxn.
  blk_t BlockCount() const { return block_count_; }

  bool IsBuffered() const { return vmoid_.id != VMOID_INVALID; }

  // Sets the source buffer for the WriteTxn to |vmoid|, and the starting block within that
  // buffer to |block_start|.
  void SetBuffer(fuchsia_hardware_block_VmoID vmoid, blk_t block_start);

  // Checks if the WriteTxn vmoid_ matches |vmoid|.
  bool CheckBuffer(fuchsia_hardware_block_VmoID vmoid) const { return vmoid_.id == vmoid.id; }

  // Resets the transaction's state.
  void Cancel() {
    requests_.reset();
    vmoid_.id = VMOID_INVALID;
    block_count_ = 0;
  }

 protected:
  // Activate the transaction, writing it out to disk.
  zx_status_t Transact();

 private:
  Bcache* bc_;
  fuchsia_hardware_block_VmoID vmoid_;  // Vmoid of the external source buffer.
  blk_t block_start_;                   // Starting block within the external source buffer.
  blk_t block_count_;                   // Total number of blocks in all requests_.
  fbl::Vector<WriteRequest> requests_;
};

#else

using WriteTxn = fs::WriteTxn;

#endif

}  // namespace minfs
