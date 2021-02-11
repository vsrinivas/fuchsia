// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SEALER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SEALER_H_

#include <zircon/types.h>

#include <memory>
#include <vector>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/geometry.h"
#include "src/devices/block/drivers/block-verity/hash-block-accumulator.h"
#include "src/lib/digest/digest.h"

namespace block_verity {

typedef void (*sealer_callback)(void* ctx, zx_status_t status, const uint8_t* buf, size_t len);

void GenerateSuperblock(const Geometry& info, uint8_t root_hash[kHashOutputSize],
                        uint8_t* block_buf);
class Sealer {
 public:
  Sealer(Geometry geometry);
  virtual ~Sealer() = default;

  // Disallow copy & assign.  Allow move.
  Sealer(const Sealer&) = delete;
  Sealer& operator=(const Sealer&) = delete;

  virtual zx_status_t StartSealing(void* cookie, sealer_callback callback);

  enum State {
    // Initial state
    Initial,

    // Still reading through data blocks, writing integrity blocks as they
    // complete
    ReadLoop,

    // Done reading through data blocks; padding out hash blocks with zeroes
    PadHashBlocks,

    // Writing out the superblock
    CommitSuperblock,

    // Requesting flush of all writes
    FinalFlush,

    // Finished.
    Done,

    // If any block operation fails along the way, mark the whole thing as a
    // failure.
    Failed,
  };

 protected:
  // Based on current state: either take an action (request an I/O) or advance
  // the state machine.
  void ScheduleNextWorkUnit();

  // Request the next data block(s) from disk so we can hash them.
  void RequestNextRead();

  // Check if any integrity accumulators are full.  If so, write them out and
  // prepare new empty ones.
  void WriteIntegrityIfReady();

  // Prepares the superblock into block_buf based on the geometry information
  // and root hash given.
  void PrepareSuperblock(uint8_t* block_buf);

  // Mark the computation as failed and trigger the sealer's callback.
  void Fail(zx_status_t error);

  // Virtual functions for providing concrete I/O implementations and their
  // expected callbacks.

  // Requests to read the block at the absolute block position `block`.  Expects
  // `CompleteRead` to be called with the I/O's status and (if successful) the
  // contents of the block read.
  virtual void RequestRead(uint64_t block) = 0;
  // The function that should be called back when the read request completes.
  void CompleteRead(zx_status_t status, uint8_t* block_data);

  // Requests to write the contents of the (full) HashBlockAccumulator to the
  // integrity block at `integrity_block`.  Expects CompleteIntegrityWrite to be
  // called with the I/O's status upon completion.
  virtual void WriteIntegrityBlock(HashBlockAccumulator& hba, uint64_t block) = 0;
  // The function that should be called back when the write request completes.
  void CompleteIntegrityWrite(zx_status_t status);

  // Requests that the I/O implementation call `PrepareSuperblock` with a
  // suitable buffer, then write the contents of the buffer perpared to the
  // zeroth block of the device.
  virtual void WriteSuperblock() = 0;
  // The function that should be called back when the write request completes.
  void CompleteSuperblockWrite(zx_status_t status);

  // Requests that the I/O implementation flush all pending writes, then call
  // `CompleteFlush`.
  virtual void RequestFlush() = 0;
  // The function that should be called back when the flush request completes.
  void CompleteFlush(zx_status_t status);

  // Drive geometry information
  const Geometry geometry_;

  // The current state of the sealing computation.
  State state_;

  // The index into the integrity section of the first integrity block that we
  // have *not* written out yet.
  IntegrityBlockIndex integrity_block_index_;

  // The first block in the data section that we have *not* requested a block read for yet.
  DataBlockIndex data_block_index_;

  // Accumulate hashes into blocks.  One for the current block-in-progress at
  // each tier of the hash tree.
  std::vector<HashBlockAccumulator> hash_block_accumulators_;

  // Hash of the root block of the merkle tree.
  uint8_t root_hash_[kHashOutputSize];

  // Hash of the superblock; the final seal;
  uint8_t final_seal_[kHashOutputSize];

  // Holds the callback function and context pointer across async boundaries.
  // Saved when StartSealing is called and called exactly once.
  sealer_callback callback_;
  void* cookie_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SEALER_H_
