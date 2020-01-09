// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <array>

#include <fs/journal/internal/inspector_parser.h>

namespace fs {

JournalInfo GetJournalSuperblock(storage::BlockBuffer* buffer) {
  return *reinterpret_cast<JournalInfo*>(buffer->Data(0));
}

std::array<uint8_t, kJournalBlockSize> GetBlockEntry(storage::BlockBuffer* buffer, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() - kJournalMetadataBlocks);
  std::array<uint8_t, kJournalBlockSize> entry;
  memcpy(entry.data(), buffer->Data(fs::kJournalMetadataBlocks + index), kJournalBlockSize);
  return entry;
}

}  // namespace fs
