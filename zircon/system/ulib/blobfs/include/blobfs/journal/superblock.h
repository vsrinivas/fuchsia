// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_JOURNAL_SUPERBLOCK_H_
#define BLOBFS_JOURNAL_SUPERBLOCK_H_

#include <zircon/types.h>

#include <blobfs/block-buffer.h>
#include <blobfs/format.h>
#include <fbl/macros.h>
#include <fs/trace.h>

namespace blobfs {

// Contains and manages state representing the on-device journal info block.
class JournalSuperblock {
 public:
  JournalSuperblock();
  explicit JournalSuperblock(std::unique_ptr<BlockBuffer> buffer);

  // Confirms that the magic and checksums within the info block
  // are correct.
  //
  // Returns |ZX_ERR_IO| if these fields do not match the expected value.
  zx_status_t Validate() const;

  // Updates all client-visible fields of the info block. Additionally updates
  // the checksum and sequence_number in-memory.
  void Update(uint64_t start, uint64_t sequence_number);

  // Returns the start of the first journal entry.
  uint64_t start() const { return Info()->start_block; }
  uint64_t sequence_number() const { return Info()->timestamp; }
  const BlockBuffer& buffer() const { return *buffer_; }

 private:
  uint32_t new_checksum() const;

  uint32_t old_checksum() const { return Info()->checksum; }

  const JournalInfo* Info() const { return reinterpret_cast<const JournalInfo*>(buffer_->Data(0)); }

  JournalInfo* Info() { return reinterpret_cast<JournalInfo*>(buffer_->Data(0)); }

  std::unique_ptr<BlockBuffer> buffer_;
};

}  // namespace blobfs

#endif  // BLOBFS_JOURNAL_SUPERBLOCK_H_
