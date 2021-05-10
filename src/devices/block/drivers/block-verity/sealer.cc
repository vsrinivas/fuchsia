// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/sealer.h"

#include <endian.h>
#include <zircon/assert.h>

#include "src/devices/block/drivers/block-verity/hash-block-accumulator.h"
#include "src/devices/block/drivers/block-verity/superblock.h"

namespace block_verity {

void GenerateSuperblock(const Geometry& geometry, uint8_t root_hash[kHashOutputSize],
                        uint8_t* block_buf) {
  // Construct a valid superblock in the memory pointed to by block_buf.
  // block_buf must be valid and have space for at least kBlockSize bytes.
  //
  // A v1 superblock looks like:
  //
  // 16 bytes magic
  // 8 bytes block count (little-endian)
  // 4 bytes block size (little-endian)
  // 4 bytes hash function tag (little-endian)
  // 32 bytes integrity root hash
  // 4032 zero bytes padding the rest of the block

  Superblock superblock;
  memset(&superblock, 0, sizeof(superblock));

  memcpy(superblock.magic, kBlockVerityMagic, sizeof(kBlockVerityMagic));
  superblock.block_count = htole64(geometry.total_blocks_);
  superblock.block_size = htole32(geometry.block_size_);
  superblock.hash_function = htole32(kSHA256HashTag);
  memcpy(superblock.integrity_root_hash, root_hash, kHashOutputSize);

  // Copy prepared superblock to target block_buf.
  memcpy(block_buf, &superblock, kBlockSize);
}

Sealer::Sealer(Geometry geometry)
    : geometry_(geometry), state_(Initial), integrity_block_index_(0), data_block_index_(0) {
  for (uint32_t i = 0; i < geometry.allocation_.integrity_shape.tree_depth; i++) {
    hash_block_accumulators_.emplace_back(HashBlockAccumulator());
  }
}

zx_status_t Sealer::StartSealing(void* cookie, sealer_callback callback) {
  if (state_ != Initial) {
    return ZX_ERR_BAD_STATE;
  }

  // Save the callback & userdata.
  cookie_ = cookie;
  callback_ = callback;

  // The overall algorithm here is:
  // * while data_blocks is not at the end of the data segment:
  //   * READ the next data block into memory
  //   * hash the contents of that block
  //   * feed that hash result into the 0-level integrity block accumulator
  //   * while any block accumulator has a full block (from lowest tier to highest):
  //     * if block is full, WRITE out the block
  //     * then hash the block and feed it into the next integrity block accumulator
  //     * then reset this level's block accumulator
  // * then pad out the remaining blocks and WRITE them all out
  // * then take the hash of the root block and put it in the superblock and
  //   WRITE the superblock out
  // * then FLUSH everything
  // * then hash the superblock itself and mark sealing as complete

  // But to start: all we need to do is set state to ReadLoop, and request the
  // first read.  Every continuation will either schedule the next additional I/O,
  // or call ScheduleNextWorkUnit() (which is the main state-machine-advancing
  // loop).
  state_ = ReadLoop;
  ScheduleNextWorkUnit();
  return ZX_OK;
}

void Sealer::ScheduleNextWorkUnit() {
  switch (state_) {
    case Initial:
      ZX_ASSERT_MSG(false, "ScheduleNextWorkUnit called while state was Initial");
      return;
    case ReadLoop:
      // See if we have read everything.  If not, dispatch a read.
      if (data_block_index_ < geometry_.allocation_.data_block_count) {
        RequestNextRead();
        return;
      } else {
        // Otherwise, update state, then fall through to PadHashBlocks.
        state_ = PadHashBlocks;
        __FALLTHROUGH;
      }
    case PadHashBlocks: {
      // For each hash tier that is not already empty (since we eagerly flush
      // full blocks), pad it with zeroes until it is full, and flush it to disk.
      for (size_t tier = 0; tier < hash_block_accumulators_.size(); tier++) {
        auto& hba = hash_block_accumulators_[tier];
        if (!hba.IsEmpty()) {
          hba.PadBlockWithZeroesToFill();
          WriteIntegrityIfReady();
          return;
        }
      }
      // If all hash tiers have been fully written out, proceed to writing out
      // the superblock.
      state_ = CommitSuperblock;
      __FALLTHROUGH;
    }
    case CommitSuperblock:
      WriteSuperblock();
      return;
    case FinalFlush: {
      RequestFlush();
      return;
    }
    case Done:
      ZX_ASSERT_MSG(false, "ScheduleNextWorkUnit called while state was Done");
      return;
    case Failed:
      ZX_ASSERT_MSG(false, "ScheduleNextWorkUnit called while state was Failed");
      return;
  }
}

void Sealer::RequestNextRead() {
  // TODO(perf optimization): adjust this implementation to read up to as many
  // blocks as will fill an integrity block at a time.  It's a convenient batch
  // size.
  uint64_t mapped_data_block = geometry_.AbsoluteLocationForData(data_block_index_);
  data_block_index_++;
  RequestRead(mapped_data_block);
}

void Sealer::WriteIntegrityIfReady() {
  // for each block accumulator:
  //   if full:
  //     if not write_requested:
  //       mark write requested
  //       send write request
  //       return
  //     else:
  //       if (not root hash block):
  //         feed hash output up a level
  //       else:
  //         save root hash for superblock
  //       reset this tier's hash block accumulator
  // if done, schedule next work unit

  for (size_t tier = 0; tier < hash_block_accumulators_.size(); tier++) {
    HashBlockAccumulator& hba = hash_block_accumulators_[tier];
    if (hba.IsFull()) {
      if (!hba.HasWriteRequested()) {
        uint64_t mapped_integrity_block =
            geometry_.AbsoluteLocationForIntegrity(integrity_block_index_);
        integrity_block_index_++;
        hba.MarkWriteRequested();
        WriteIntegrityBlock(hba, mapped_integrity_block);

        return;
      } else {
        // We previously marked this write as requested and have now completed it.
        // We should now hash this block and feed it into the next hash block
        // accumulator up.  That block might now be full, so we continue the
        // top-level for loop.

        digest::Digest hasher;
        const uint8_t* block_hash = hasher.Hash(hba.BlockData(), kBlockSize);

        if (tier + 1 < hash_block_accumulators_.size()) {
          // Some tier other than the last.  Feed integrity block into parent.
          HashBlockAccumulator& next_tier_hba = hash_block_accumulators_[tier + 1];
          next_tier_hba.Feed(block_hash, hasher.len());
        } else {
          // This is the final tier.  Save the root hash so we can put it in the
          // superblock.
          memcpy(root_hash_, block_hash, hasher.len());
        }

        hba.Reset();
      }
    }
  }

  // If we made it here, we've finished flushing all hash blocks that we've fed
  // in enough input to complete.
  ScheduleNextWorkUnit();
}

void Sealer::PrepareSuperblock(uint8_t* block_buf) {
  GenerateSuperblock(geometry_, root_hash_, block_buf);

  // Save the superblock hash to return to the caller upon successful flush.
  digest::Digest hasher;
  const uint8_t* hashed = hasher.Hash(reinterpret_cast<void*>(block_buf), kBlockSize);
  memcpy(final_seal_, hashed, hasher.len());
}

void Sealer::Fail(zx_status_t error) {
  state_ = Failed;
  // Notify computation completion (failed)
  if (callback_) {
    sealer_callback callback = callback_;
    callback_ = nullptr;
    void* cookie = cookie_;
    cookie_ = nullptr;
    callback(cookie, error, nullptr, 0);
  }
  return;
}

void Sealer::CompleteRead(zx_status_t status, uint8_t* block_data) {
  // Check for failures.
  if (status != ZX_OK) {
    Fail(status);
    return;
  }

  // Hash the contents of the block we just read.
  // TODO: read batching
  digest::Digest hasher;
  hasher.Hash(reinterpret_cast<void*>(block_data), kBlockSize);

  // Feed that hash result into the 0-level integrity block accumulator.
  hash_block_accumulators_[0].Feed(hasher.get(), hasher.len());

  // then check if we need to flush any out to disk
  WriteIntegrityIfReady();
}

void Sealer::CompleteIntegrityWrite(zx_status_t status) {
  // Check for failures.
  if (status != ZX_OK) {
    Fail(status);
    return;
  }

  // Continue updating integrity blocks until flushed.
  WriteIntegrityIfReady();
}

void Sealer::CompleteSuperblockWrite(zx_status_t status) {
  // Check for failures.
  if (status != ZX_OK) {
    Fail(status);
    return;
  }

  state_ = FinalFlush;
  ScheduleNextWorkUnit();
}

void Sealer::CompleteFlush(zx_status_t status) {
  // Check for failures.
  if (status != ZX_OK) {
    Fail(status);
    return;
  }

  state_ = Done;

  ZX_ASSERT(callback_);
  sealer_callback callback = callback_;
  callback_ = nullptr;
  void* cookie = cookie_;
  cookie_ = nullptr;
  // Calling the callback must be the very last thing we do.  We expect
  // `this` to be deallocated in the course of the callback here.
  callback(cookie, ZX_OK, final_seal_, kHashOutputSize);
}

}  // namespace block_verity
