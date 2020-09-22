// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_REPLAY_H_
#define FS_JOURNAL_REPLAY_H_

#include <zircon/types.h>

#include <fs/journal/superblock.h>
#include <fs/transaction/transaction_handler.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/operation/operation.h>

#include "lib/zx/status.h"

namespace fs {

// Parses all entries within the journal, and returns the operations which must be
// replayed to return the filesystem to a consistent state. Additionally returns the sequence number
// which should be used to update the info block once replay has completed successfully.
//
// This function is invoked by |ReplayJournal|. Refer to that function for the common case
// of replaying a journal on boot.
zx_status_t ParseJournalEntries(const JournalSuperblock* info, storage::VmoBuffer* journal_buffer,
                                std::vector<storage::BufferedOperation>* operations,
                                uint64_t* out_sequence_number, uint64_t* out_start);

// Replays the entries in the journal, first parsing them, and later writing them
// out to disk.
// |journal_start| is the start of the journal area (includes info block).
// |journal_length| is the length of the journal area (includes info block).
//
// Returns the new |JournalSuperblock|, with an updated sequence number which should
// be used on journal initialization.
zx::status<JournalSuperblock> ReplayJournal(fs::TransactionHandler* transaction_handler,
                                            storage::VmoidRegistry* registry,
                                            uint64_t journal_start, uint64_t journal_length,
                                            uint32_t block_size);

}  // namespace fs

#endif  // FS_JOURNAL_REPLAY_H_
