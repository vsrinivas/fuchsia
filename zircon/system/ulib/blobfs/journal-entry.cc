// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/journal_entry.h>

#include <blobfs/journal.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/cksum.h>
#include <zircon/types.h>

#include <utility>

namespace blobfs {

JournalEntry::JournalEntry(JournalWriter* journal, uint64_t header_index, uint64_t commit_index,
                           fbl::unique_ptr<WritebackWork> work, bool dummy)
        : is_dummy_(dummy), journal_(journal), block_count_(0),
          header_index_(header_index), commit_index_(commit_index), work_(std::move(work)) {
    size_t work_blocks = work_->Transaction().BlkCount();
    if (is_dummy_ || !work_blocks) {
        return;
    }

    // Ensure the work is valid.
    ZX_DEBUG_ASSERT(work_->Transaction().IsBuffered());
    ZX_DEBUG_ASSERT(work_blocks <= kMaxEntryDataBlocks);

    // Copy all target blocks from the WritebackWork to the entry's header block.
    for (size_t i = 0; i < work_->Transaction().Operations().size(); i++) {
        UnbufferedOperation& operation = work_->Transaction().Operations()[i];
        for (size_t j = operation.op.dev_offset;
             j < operation.op.dev_offset + operation.op.length; j++) {
            header_block_.target_blocks[block_count_++] = j;
        }
    }

    ZX_DEBUG_ASSERT(work_blocks == block_count_);

    // Set other information in the header/commit blocks.
    header_block_.magic = kEntryHeaderMagic;
    header_block_.num_blocks = block_count_;
    header_block_.timestamp = zx_ticks_get();
    commit_block_.magic = kEntryCommitMagic;
    commit_block_.timestamp = header_block_.timestamp; commit_block_.checksum = 0;
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
        fbl::AutoLock lock(&lock_);
        // If the entry is in a waiting state, it is ready to be written to disk.
        return next_state_ != State::kWriteJournalSlot;
    };
}

SyncCallback JournalEntry::CreateSyncCallback() {
    return [this] (zx_status_t result) {
        // Signal the journal that an entry is ready for processing.
        journal_->ProcessEntryResult(result, this);
    };
}

void JournalEntry::SetStatusFromResult(zx_status_t result) {
    {
        // Unfortunately this is called from the write-back thread.
        fbl::AutoLock lock(&lock_);
        if (next_state_ == State::kWriteJournalSlot) {
            // The write back may call back even before we say we are ready to
            // start processing. This should go away with explicit callbacks
            // when sending items to write. In the mean time, complete the
            // operation we are being notified about prematurely.
            ZX_DEBUG_ASSERT(result == ZX_ERR_BAD_STATE);
            DoWriteJournalEntryComplete(result);
            return;
        }
    }
    ZX_DEBUG_ASSERT(GetStatus() == ZX_ERR_ASYNC);
    OnOperationComplete(result);
}

void JournalEntry::SetChecksum(uint32_t checksum) {
    commit_block_.checksum = checksum;
}

zx_status_t JournalEntry::GetStatus() const {
    fbl::AutoLock lock(&lock_);
    switch (next_state_) {
    case State::kWriteJournalSlot:
    case State::kWriteData:
    case State::kDeleteJournalSlot:
        return last_status_;

    case State::kWriteJournalSlotComplete:
    case State::kWriteDataComplete:
    case State::kDeleteJournalSlotComplete:
        return ZX_ERR_ASYNC;

    case State::kDone:
        return ZX_ERR_STOP;

    case State::kUnset:
        break;
    }
    return ZX_ERR_BAD_STATE;
}

// The current flow is to move sequentially through all the states.
//
// For normal entries, that means:
//   - Write data to the journal location.
//   - Write data to the final location.
//   - Delete data from the journal location.
//
// Sync entries just wait for the journal to take action at each completion step
// without issuing any async operation, until the last step, when instead of
// deleting data, the sync work item is sent to the write back queue.
//
// Individual states return ZX_OK to move to the next state and ZX_ERR_ASYNC  or
// ZX_ERR_SHOULD_WAIT to signal the FSM is waiting either for completion of a
// pending operation or for the journal to acknowledge the state, respectively.
void JournalEntry::DoLoop(zx_status_t result) {
    fbl::AutoLock lock(&lock_);
    do {
        State state;
        state = next_state_;
        next_state_ = State::kUnset;

        switch (state) {
        case State::kWriteJournalSlot:
            ZX_DEBUG_ASSERT(result == ZX_OK);
            result = DoWriteJournalEntry();
            break;
        case State::kWriteJournalSlotComplete:
            result = DoWriteJournalEntryComplete(result);
            break;
        case State::kWriteData:
            ZX_DEBUG_ASSERT(result == ZX_OK);
            result = DoWriteData();
            break;
        case State::kWriteDataComplete:
            result = DoWriteDataComplete(result);
            break;
        case State::kDeleteJournalSlot:
            ZX_DEBUG_ASSERT(result == ZX_OK);
            result = DoDeleteEntry();
            break;
        case State::kDeleteJournalSlotComplete:
            result = DoDeleteEntryComplete(result);
            break;
        case State::kDone:
        case State::kUnset:
            ZX_DEBUG_ASSERT(false);
            break;
        }
    } while (result != ZX_ERR_ASYNC && result != ZX_ERR_SHOULD_WAIT && next_state_ != State::kDone);
}

zx_status_t JournalEntry::DoWriteJournalEntry() {
    // Start writing the journal entry to disk.
    zx_status_t status = ZX_OK;
    if (HasData()) {
        journal_->WriteEntry(this);
        status = ZX_ERR_ASYNC;
    }
    TransitionTo(State::kWriteJournalSlotComplete);
    return status;
}

zx_status_t JournalEntry::DoWriteJournalEntryComplete(zx_status_t result) {
    // Journal entry is written. Wait for journal to proceed.
    last_status_ = result;
    TransitionTo(State::kWriteData);
    return ZX_ERR_SHOULD_WAIT;
}

zx_status_t JournalEntry::DoWriteData() {
    // Start writing data to final disk location.
    zx_status_t status = ZX_OK;
    if (HasData()) {
        journal_->EnqueueEntryWork(TakeWork());
        status = ZX_ERR_ASYNC;
    }
    TransitionTo(State::kWriteDataComplete);
    return status;
}

zx_status_t JournalEntry::DoWriteDataComplete(zx_status_t result) {
    // Data is now on final destination. Wait for journal to proceed.
    last_status_ = result;
    TransitionTo(State::kDeleteJournalSlot);
    return ZX_ERR_SHOULD_WAIT;
}

zx_status_t JournalEntry::DoDeleteEntry() {
    // Update journal entry on disk (delete).
    if (HasData()) {
        journal_->DeleteEntry(this);
    } else {
        journal_->EnqueueEntryWork(TakeWork());
    }
    TransitionTo(State::kDeleteJournalSlotComplete);
    return ZX_OK;
}

zx_status_t JournalEntry::DoDeleteEntryComplete(zx_status_t result) {
    // Entry was removed from journal.
    last_status_ = result;
    TransitionTo(State::kDone);
    return ZX_OK;
}

void JournalEntry::TransitionTo(State state) {
    ZX_DEBUG_ASSERT(next_state_ != state);
    next_state_ = state;
}

} // blobfs
