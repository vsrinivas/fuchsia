// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal.h>
#include <fbl/unique_ptr.h>
#include <lib/cksum.h>
#include <zircon/types.h>

#include <utility>

namespace blobfs {

JournalEntry::JournalEntry(JournalBase* journal, EntryStatus status, size_t header_index,
                           size_t commit_index, fbl::unique_ptr<WritebackWork> work)
        : journal_(journal), status_(static_cast<uint32_t>(status)), block_count_(0),
          header_index_(header_index), commit_index_(commit_index), work_(std::move(work)) {
    if (status != EntryStatus::kInit) {
        // In the case of a sync request or error, return early.
        ZX_DEBUG_ASSERT(status == EntryStatus::kSync || status == EntryStatus::kError);
        return;
    }

    size_t work_blocks = work_->Transaction().BlkCount();
    // Ensure the work is valid.
    ZX_DEBUG_ASSERT(work_blocks > 0);
    ZX_DEBUG_ASSERT(work_->Transaction().IsBuffered());
    ZX_DEBUG_ASSERT(work_blocks <= kMaxEntryDataBlocks);

    // Copy all target blocks from the WritebackWork to the entry's header block.
    for (size_t i = 0; i < work_->Transaction().Requests().size(); i++) {
        WriteRequest& request = work_->Transaction().Requests()[i];
        for (size_t j = request.dev_offset; j < request.dev_offset + request.length; j++) {
            header_block_.target_blocks[block_count_++] = j;
        }
    }

    ZX_DEBUG_ASSERT(work_blocks == block_count_);

    // Set other information in the header/commit blocks.
    header_block_.magic = kEntryHeaderMagic;
    header_block_.num_blocks = block_count_;
    header_block_.timestamp = zx_ticks_get();
    commit_block_.magic = kEntryCommitMagic;
    commit_block_.timestamp = header_block_.timestamp;
    commit_block_.checksum = 0;
}

fbl::unique_ptr<WritebackWork> JournalEntry::TakeWork() {
    ZX_DEBUG_ASSERT(work_ != nullptr);

    if (header_index_ != commit_index_) {
        // If the journal entry contains any transactions, set the work closure to update the entry
        // status on write completion. This currently assumes that a WritebackWork with associated
        // transactions will NOT already have a closure attached. If we ever want to include
        // transactions on a syncing WritebackWork, we will need to revisit this.
        work_->SetSyncCallback(CreateSyncCallback());
    }

    return std::move(work_);
}

ReadyCallback JournalEntry::CreateReadyCallback() {
    return [this] () {
        // If the entry is in a waiting state, it is ready to be written to disk.
        return GetStatus() == EntryStatus::kWaiting;
    };
}

SyncCallback JournalEntry::CreateSyncCallback() {
    return [this] (zx_status_t result) {
        // Signal the journal that an entry is ready for processing.
        journal_->ProcessEntryResult(result, this);
    };
}

void JournalEntry::SetStatusFromResult(zx_status_t result) {
    // Set the state of the JournalEntry based on the last received result.
    if (result != ZX_OK) {
        SetStatus(EntryStatus::kError);
        return;
    }

    EntryStatus last_status = SetStatus(EntryStatus::kPersisted);
    ZX_DEBUG_ASSERT(last_status == EntryStatus::kWaiting);
}

void JournalEntry::SetChecksum(uint32_t checksum) {
    commit_block_.checksum = checksum;
}

} // blobfs
