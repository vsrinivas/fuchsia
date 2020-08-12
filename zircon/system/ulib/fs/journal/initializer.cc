// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <sys/mman.h>
#include <zircon/errors.h>

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

zx_status_t MakeJournal(uint64_t journal_blocks, const WriteBlocksFn& WriteBlocks) {
  uint8_t block[kJournalBlockSize];
  fbl::Span<uint8_t> buffer(block, sizeof(block));
  InitJournalBlock(buffer);

  auto status = WriteBlocks(buffer, 0, 1);
  if (status != ZX_OK) {
    return status;
  }

  // If number of journal metadata blocks change, we need to clear/initialize
  // those blocks. This compile-time assert prevents having uninitialized metadata
  // blocks.
  static_assert(fs::kJournalMetadataBlocks == 1, "Uninitialized blocks in journal");

  // Clear the journal from disk.
  uint64_t block_count = journal_blocks - kJournalMetadataBlocks;
  size_t map_length = kJournalBlockSize * block_count;
  void* blocks = mmap(nullptr, map_length, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (blocks == MAP_FAILED) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::Span<const uint8_t> buffers(static_cast<const uint8_t*>(blocks), map_length);
  status = WriteBlocks(buffers, kJournalMetadataBlocks, block_count);

  if (munmap(blocks, map_length) != 0) {
    return ZX_ERR_NO_MEMORY;
  }

  return status;
}

}  // namespace fs
