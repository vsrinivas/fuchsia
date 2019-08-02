// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_JOURNAL_REPLAY_H_
#define BLOBFS_JOURNAL_REPLAY_H_

#include <zircon/types.h>

#include <blobfs/journal/superblock.h>
#include <blobfs/operation.h>
#include <blobfs/vmo-buffer.h>
#include <fbl/vector.h>
#include <fs/block-txn.h>

namespace blobfs {

// Parses all entries within the journal, and returns the operations which must be
// replayed to return the filesystem to a consistent state. Additionally returns the sequence number
// which should be used to update the info block once replay has completed successfully.
//
// This function is invoked by |ReplayJournal|. Refer to that function for the common case
// of replaying a journal on boot.
zx_status_t ParseJournalEntries(const JournalSuperblock* info, VmoBuffer* journal_buffer,
                                fbl::Vector<BufferedOperation>* operations,
                                uint64_t* out_sequence_number);

// Replays the entries in the journal, first parsing them, and later writing them
// out to disk.
// |journal_start| is the start of the journal area (includes info block).
// |journal_length| is the length of the journal area (includes info block).
//
// Returns the new |JournalSuperblock|, with an updated sequence number which should
// be used on journal initialization.
zx_status_t ReplayJournal(fs::TransactionHandler* transaction_handler, VmoidRegistry* registry,
                          uint64_t journal_start, uint64_t journal_length,
                          JournalSuperblock* out_journal_superblock);

}  // namespace blobfs

#endif  // BLOBFS_JOURNAL_REPLAY_H_
