// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "entry-view.h"

#include <lib/cksum.h>
#include <zircon/types.h>

#include <fbl/vector.h>
#include <fs/journal/format.h>
#include <fs/operation/buffered_operation.h>

namespace fs {

JournalEntryView::JournalEntryView(fs::BlockBufferView view) : view_(std::move(view)) {}

JournalEntryView::JournalEntryView(fs::BlockBufferView view,
                                   const fbl::Vector<fs::BufferedOperation>& operations,
                                   uint64_t sequence_number)
    : view_(std::move(view)) {
  Encode(operations, sequence_number);
}

void JournalEntryView::Encode(const fbl::Vector<fs::BufferedOperation>& operations,
                              uint64_t sequence_number) {
  memset(header(), 0, kJournalBlockSize);
  header()->prefix.magic = kJournalEntryMagic;
  header()->prefix.sequence_number = sequence_number;
  header()->prefix.flags = kJournalPrefixFlagHeader;
  header()->payload_blocks = view_.length() - kEntryMetadataBlocks;
  ZX_DEBUG_ASSERT(header()->payload_blocks < kMaxBlockDescriptors);
  size_t block_index = 0;
  for (const auto& operation : operations) {
    for (size_t i = 0; i < operation.op.length; i++) {
      header()->target_blocks[block_index] = operation.op.dev_offset + i;
      auto block_ptr =
          reinterpret_cast<uint64_t*>(view_.Data(kJournalEntryHeaderBlocks + block_index));
      if (*block_ptr == kJournalEntryMagic) {
        // If the payload could be confused with a journal structure, replace
        // it with zeros, and add an "escaped" flag instead.
        *block_ptr = 0;
        header()->target_flags[block_index] |= kJournalBlockDescriptorFlagEscapedBlock;
      }
      block_index++;
    }
  }
  ZX_DEBUG_ASSERT_MSG(block_index == header()->payload_blocks, "Mismatched block count");

  memset(footer(), 0, sizeof(JournalCommitBlock));
  footer()->prefix.magic = kJournalEntryMagic;
  footer()->prefix.sequence_number = sequence_number;
  footer()->prefix.flags = kJournalPrefixFlagCommit;
  footer()->checksum = CalculateChecksum();
}

void JournalEntryView::DecodePayloadBlocks() {
  for (size_t i = 0; i < header()->payload_blocks; i++) {
    if (header()->target_flags[i] & kJournalBlockDescriptorFlagEscapedBlock) {
      auto block_ptr = reinterpret_cast<uint64_t*>(view_.Data(kJournalEntryHeaderBlocks + i));
      *block_ptr = kJournalEntryMagic;
    }
  }
}

uint32_t JournalEntryView::CalculateChecksum() const {
  // Currently, the checksum includes all blocks excluding the commit block.
  // If additional data is to be added to the commit block, we should consider
  // making the checksum include the commit block (excluding the checksum location).
  uint32_t checksum = 0;
  for (size_t i = 0; i < view_.length() - 1; i++) {
    checksum = crc32(checksum, static_cast<const uint8_t*>(view_.Data(i)), kJournalBlockSize);
  }
  return checksum;
}

}  // namespace fs
