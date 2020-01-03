// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "entry_view.h"

#include <lib/cksum.h>
#include <zircon/types.h>

#include <fbl/vector.h>
#include <storage/operation/buffered_operation.h>

namespace fs {

JournalEntryView::JournalEntryView(storage::BlockBufferView view)
    : view_(std::move(view)),
      header_(fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(view_.Data(0)), view_.BlockSize())) {}

JournalEntryView::JournalEntryView(storage::BlockBufferView view,
                                   const fbl::Vector<storage::BufferedOperation>& operations,
                                   uint64_t sequence_number)
    : view_(std::move(view)),
      header_(fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(view_.Data(0)), view_.BlockSize()),
              view_.length() - kEntryMetadataBlocks, sequence_number) {
  Encode(operations, sequence_number);
}

void JournalEntryView::Encode(const fbl::Vector<storage::BufferedOperation>& operations,
                              uint64_t sequence_number) {
  ZX_DEBUG_ASSERT(header_.PayloadBlocks() < kMaxBlockDescriptors);
  uint32_t block_index = 0;
  for (const auto& operation : operations) {
    for (size_t i = 0; i < operation.op.length; i++) {
      header_.SetTargetBlock(block_index, operation.op.dev_offset + i);
      auto block_ptr =
          reinterpret_cast<uint64_t*>(view_.Data(kJournalEntryHeaderBlocks + block_index));
      if (*block_ptr == kJournalEntryMagic) {
        // If the payload could be confused with a journal structure, replace
        // it with zeros, and add an "escaped" flag instead.
        *block_ptr = 0;
        header_.SetEscapedBlock(block_index, true);
      }
      block_index++;
    }
  }
  ZX_DEBUG_ASSERT_MSG(block_index == header_.PayloadBlocks(), "Mismatched block count");

  memset(footer(), 0, sizeof(JournalCommitBlock));
  footer()->prefix.magic = kJournalEntryMagic;
  footer()->prefix.sequence_number = sequence_number;
  footer()->prefix.flags = kJournalPrefixFlagCommit;
  footer()->checksum = CalculateChecksum();
}

void JournalEntryView::DecodePayloadBlocks() {
  for (uint32_t i = 0; i < header().PayloadBlocks(); i++) {
    if (header_.EscapedBlock(i)) {
      auto block_ptr = reinterpret_cast<uint64_t*>(view_.Data(kJournalEntryHeaderBlocks + i));
      ZX_ASSERT(*block_ptr == 0);
      *block_ptr = kJournalEntryMagic;
    }
  }
}

uint32_t JournalEntryView::CalculateChecksum() const {
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  // Always return 0 when fuzzing.
  return 0;
#else
  // Currently, the checksum includes all blocks excluding the commit block.
  // If additional data is to be added to the commit block, we should consider
  // making the checksum include the commit block (excluding the checksum location).
  uint32_t checksum = 0;
  for (size_t i = 0; i < view_.length() - 1; i++) {
    checksum = crc32(checksum, static_cast<const uint8_t*>(view_.Data(i)), kJournalBlockSize);
  }
  return checksum;
#endif
}

}  // namespace fs
