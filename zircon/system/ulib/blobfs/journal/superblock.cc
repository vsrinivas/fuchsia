// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <blobfs/format.h>
#include <blobfs/journal/superblock.h>
#include <fs/trace.h>

namespace blobfs {

JournalSuperblock::JournalSuperblock() = default;

JournalSuperblock::JournalSuperblock(std::unique_ptr<BlockBuffer> buffer)
    : buffer_(std::move(buffer)) {
  ZX_DEBUG_ASSERT_MSG(buffer_->capacity() > 0, "Buffer is too small for journal superblock");
}

zx_status_t JournalSuperblock::Validate() const {
  if (Info()->magic != kJournalMagic) {
    FS_TRACE_ERROR("Bad journal magic\n");
    return ZX_ERR_IO;
  }
  if (old_checksum() != new_checksum()) {
    FS_TRACE_ERROR("Bad journal info checksum\n");
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void JournalSuperblock::Update(uint64_t start, uint64_t sequence_number) {
  Info()->magic = kJournalMagic;
  Info()->start_block = start;
  Info()->timestamp = sequence_number;
  Info()->checksum = new_checksum();
}

uint32_t JournalSuperblock::new_checksum() const {
  JournalInfo info = *reinterpret_cast<const JournalInfo*>(buffer_->Data(0));
  info.checksum = 0;
  return crc32(0, reinterpret_cast<const uint8_t*>(&info), sizeof(JournalInfo));
}

}  // namespace blobfs
