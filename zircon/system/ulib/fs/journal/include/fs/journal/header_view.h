// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_HEADER_VIEW_H_
#define FS_JOURNAL_HEADER_VIEW_H_

#include <lib/fit/result.h>

#include <fbl/span.h>
#include <fs/journal/format.h>

namespace fs {

class JournalHeaderView {
 public:
  // Creates header view created from the block. This may or may not be a valid header.
  // Useful when inspecting a disk.
  // Asserts on finding |block| to be at smaller than kJournalBlockSize bytes.
  explicit JournalHeaderView(fbl::Span<uint8_t> block);

  // Returns HeaderView on finding a valid journal entry header in |block|.
  static fit::result<JournalHeaderView, zx_status_t> Create(fbl::Span<uint8_t> block,
                                                            uint64_t sequence_number);

  // Initializes |block| with valid header and sets payload blocks and sequence number.
  // Asserts on finding |block| to be at smaller than kJournalBlockSize bytes.
  JournalHeaderView(fbl::Span<uint8_t> block, uint64_t payload_blocks, uint64_t sequence_number);

  // Returns the block number where |index| block in the payload will be written to.
  uint64_t TargetBlock(uint32_t index) const {
    ZX_DEBUG_ASSERT(index < kMaxBlockDescriptors);
    return header_->target_blocks[index];
  }

  // Sets |index| block in the payload to be written to |target| block.
  void SetTargetBlock(uint32_t index, uint64_t target) {
    ZX_DEBUG_ASSERT(index < kMaxBlockDescriptors);
    header_->target_blocks[index] = target;
  }

  // Returns pointer to memory where target block for |index| is stored.
  // TODO(fxbug.dev/42430).
  const uint64_t* TargetBlockPtr(uint32_t index) const {
    ZX_DEBUG_ASSERT(index < kMaxBlockDescriptors);
    return &header_->target_blocks[index];
  }

  // Returns true if target block at |index| is escaped.
  bool EscapedBlock(uint32_t index) const {
    return TargetFlags(index) & kJournalBlockDescriptorFlagEscapedBlock;
  }

  // Sets escape flag for target block at |index|.
  void SetEscapedBlock(uint32_t index, bool flag) {
    if (flag) {
      SetTargetFlags(index, TargetFlags(index) | kJournalBlockDescriptorFlagEscapedBlock);
    } else {
      SetTargetFlags(index, TargetFlags(index) & (~kJournalBlockDescriptorFlagEscapedBlock));
    }
  }

  uint64_t PayloadBlocks() const { return header_->payload_blocks; }

  // TODO(fxbug.dev/42430).
  const uint64_t* PayloadBlocksPtr() const { return &header_->payload_blocks; }
  JournalObjectType ObjectType() const { return header_->prefix.ObjectType(); }

  uint64_t SequenceNumber() const { return header_->prefix.sequence_number; }

 private:
  // Zeroes kJournalBlockSize bytes of |header_| and initializes fields of JournalHeaderBlock.
  void Encode(uint64_t payload_blocks, uint64_t sequence_number);

  // Returns the flags set for |index| block in the payload.
  uint32_t TargetFlags(uint32_t index) const {
    ZX_DEBUG_ASSERT(index < kMaxBlockDescriptors);
    return header_->target_flags[index];
  }

  // Sets flags for |index| block in the payload.
  void SetTargetFlags(uint32_t index, uint32_t flags) {
    ZX_DEBUG_ASSERT(index < kMaxBlockDescriptors);
    header_->target_flags[index] = flags;
  }

  JournalHeaderBlock* header_ = nullptr;
};

}  // namespace fs

#endif  // FS_JOURNAL_HEADER_VIEW_H_
