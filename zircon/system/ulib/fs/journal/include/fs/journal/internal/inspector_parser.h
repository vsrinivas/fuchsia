// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_INTERNAL_INSPECTOR_PARSER_H_
#define FS_JOURNAL_INTERNAL_INSPECTOR_PARSER_H_

#include <zircon/types.h>

#include <array>

#include <fs/journal/format.h>
#include <storage/buffer/block_buffer.h>

namespace fs {

// Parses the first block in the passed in BlockBuffer as the journal superblock.
JournalInfo GetJournalSuperblock(storage::BlockBuffer* buffer);

// Parses the blocks starting from the second block as journal entries.
// Note: This method currently indexes using absolute block position in the journal
// and not based on start_block defined in the journal superblock. It is also a hackish
// way to access journal entry blocks for compatability with how disk-inspect is
// currently parsing the journal.
// TODO(fxbug.dev/42430): Change how this method works once journal parsing and disk-inspect
// frontend is reworked.
std::array<uint8_t, kJournalBlockSize> GetBlockEntry(storage::BlockBuffer* buffer, uint64_t index);

}  // namespace fs

#endif  // FS_JOURNAL_INTERNAL_INSPECTOR_PARSER_H_
