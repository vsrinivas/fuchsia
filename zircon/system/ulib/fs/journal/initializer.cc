// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>

#include <fs/journal/format.h>
#include <fs/journal/initializer.h>

namespace fs {
namespace {

void InitJournalBlock(fbl::Span<uint8_t> block) {
  memset(block.data(), 0, block.size());
  JournalInfo* info = reinterpret_cast<JournalInfo*>(block.data());
  info->magic = kJournalMagic;

  // TODO(42698): This checksum should be on entire block and not just JournalInfo.
  info->checksum = crc32(0, block.data(), sizeof(fs::JournalInfo));
}

}  // namespace

zx_status_t MakeJournal(uint64_t journal_blocks, WriteBlockFn WriteBlock) {
  uint8_t block[kJournalBlockSize];
  fbl::Span<uint8_t> buffer(block, sizeof(block));
  InitJournalBlock(buffer);

  auto status = WriteBlock(buffer, 0);
  if (status != ZX_OK) {
    return status;
  }

  // If number of journal metadata blocks change, we need to clear/initialize
  // those blocks. This compile-time assert prevents having uninitialized metadata
  // blocks.
  static_assert(fs::kJournalMetadataBlocks == 1, "Uninitialized blocks in journal");

  // Clear the journal from disk.
  memset(block, 0, sizeof(block));

  for (uint32_t i = fs::kJournalMetadataBlocks; i < journal_blocks; i++) {
    status = WriteBlock(buffer, i);
    if (status != ZX_OK) {
      return status;
    }
  }

  return status;
}

}  // namespace fs
