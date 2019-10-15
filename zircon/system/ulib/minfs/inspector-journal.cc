// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector-journal.h"

#include <lib/disk-inspector/common-types.h>

#include "inspector-journal-entries.h"
#include "inspector-private.h"

namespace minfs {

void JournalObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> JournalObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0: {
      // uint64_t magic.
      return CreateUint64DiskObj("magic", &(journal_info_.magic));
    }
    case 1: {
      // uint64_t start_block
      return CreateUint64DiskObj("start_block", &(journal_info_.start_block));
    }
    case 2: {
      // uint64_t reserved
      return CreateUint64DiskObj("reserved", &(journal_info_.reserved));
    }
    case 3: {
      // uint64_t timestamp
      return CreateUint64DiskObj("timestamp", &(journal_info_.timestamp));
    }
    case 4: {
      // uint64_t checksum
      return CreateUint32DiskObj("checksum", &(journal_info_.checksum));
    }
    case 5: {
      return std::make_unique<JournalEntries>(journal_info_,
                                              start_block_ + fs::kJournalMetadataBlocks,
                                              length_ - fs::kJournalMetadataBlocks, fs_);
    }
  }
  return nullptr;
}

}  // namespace minfs
