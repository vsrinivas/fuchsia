// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <string.h>

#include <fs/journal/header_view.h>

namespace fs {

namespace {

// Returns true if the |header| has kJournalEntryMagic as magic number and matches
// the |sequence_number|
bool IsJournalMetadata(const JournalHeaderBlock* header, uint64_t sequence_number) {
  if (header->prefix.magic != kJournalEntryMagic) {
    return false;
  }
  if (header->prefix.sequence_number != sequence_number) {
    return false;
  }
  return true;
}

// Returns true if the |header| IsJournalMetadata and is of type JournalObjectType::kHeader
// or JournalObjectType::kRevocation.
bool IsHeader(const JournalHeaderBlock* header, uint64_t sequence_number) {
  if (!IsJournalMetadata(header, sequence_number)) {
    return false;
  }
  if (header->prefix.ObjectType() != JournalObjectType::kHeader &&
      header->prefix.ObjectType() != JournalObjectType::kRevocation) {
    return false;
  }
  return true;
}

}  // namespace

JournalHeaderView::JournalHeaderView(fbl::Span<uint8_t> block)
    : header_(reinterpret_cast<JournalHeaderBlock*>(block.data())) {
  ZX_ASSERT(block.size_bytes() >= kJournalBlockSize);
}

JournalHeaderView::JournalHeaderView(fbl::Span<uint8_t> block, uint64_t payload_blocks,
                                     uint64_t sequence_number)
    : header_(reinterpret_cast<JournalHeaderBlock*>(block.data())) {
  ZX_ASSERT(block.size_bytes() >= kJournalBlockSize);
  Encode(payload_blocks, sequence_number);
}

void JournalHeaderView::Encode(uint64_t payload_blocks, uint64_t sequence_number) {
  memset(header_, 0, kJournalBlockSize);
  header_->prefix.magic = kJournalEntryMagic;
  header_->prefix.sequence_number = sequence_number;
  header_->prefix.flags = kJournalPrefixFlagHeader;
  header_->payload_blocks = payload_blocks;
}

fit::result<JournalHeaderView, zx_status_t> JournalHeaderView::Create(fbl::Span<uint8_t> block,
                                                                      uint64_t sequence_number) {
  if (block.size_bytes() < kJournalBlockSize) {
    return fit::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  if (!IsHeader(reinterpret_cast<const JournalHeaderBlock*>(block.data()), sequence_number)) {
    return fit::error(ZX_ERR_BAD_STATE);
  }
  return fit::ok(JournalHeaderView(block));
}
}  // namespace fs
