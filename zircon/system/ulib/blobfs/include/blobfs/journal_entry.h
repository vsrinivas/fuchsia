// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(rvargas): This should not be part of the exported library interface.

#pragma once

#include <cstdint>
#include <memory>

#include <blobfs/writeback-work.h>
#include <fbl/intrusive_single_list.h>
#include <zircon/types.h>

namespace blobfs {

class JournalEntry;

using ReadyCallback = blobfs::WritebackWork::ReadyCallback;
using SyncCallback = fs::Vnode::SyncCallback;

// The journal from the point of view of a JournalEntry.
class JournalWriter {
  public:
    JournalWriter() = default;
    virtual ~JournalWriter() = default;

    // Process the |result| from the last operation performed on |entry|. This should be invoked as
    // part of the sync callback from the writeback thread.
    // This method will go away once we move to explicit callbacks.
    virtual void ProcessEntryResult(zx_status_t result, JournalEntry* entry) = 0;

    // Writes the entry to the journal location.
    virtual void WriteEntry(JournalEntry* entry) = 0;

    // Deletes the entry from the journal.
    virtual void DeleteEntry(JournalEntry* entry) = 0;

    // Enqueues transactions from the entry buffer to the blobfs writeback queue.
    // Verifies the transactions and sets the buffer if necessary.
    virtual zx_status_t EnqueueEntryWork(fbl::unique_ptr<WritebackWork> work) = 0;
};

// Represents a single entry within the Journal, including header and commit block indices
// and contents, and the WritebackWork representing the entry's data. Contains state indicating
// whether the entry has been processed. The JournalEntry lifetime should never exceed that of the
// journal that owns it. The JournalEntry must be kept alive until all callbacks have been invoked
// and the entry is ultimately removed from the journal.
// During the lifetime of a JournalEntry, it is written to the journal, written to disk, and
// deleted from the journal. At each step a callback is invoked to update the state of the entry to
// reflect the success of the operation. Some entries are "sync" entries, which have no associated
// journal data, and are only invoked once all entries queued before them have been fully processed.
class JournalEntry : public fbl::SinglyLinkedListable<fbl::unique_ptr<JournalEntry>>  {
  public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(JournalEntry);

    // is_dummy tells whether this is a "regular" entry or an "error" entry,
    // used to signal that the journal is in read-only mode.
    // TODO(rvargas): remove this behavior.
    JournalEntry(JournalWriter* journal, uint64_t header_index, uint64_t commit_index,
                 fbl::unique_ptr<WritebackWork> work, bool is_dummy);

    // Forcibly resets the associated WritebackWork. This should only be called in the event of an
    // error; i.e. blobfs has transitioned to a readonly state. This reset should also resolve any
    // pending sync closures within the work.
    void ForceReset() {
        if (work_ != nullptr) {
            work_->MarkCompleted(ZX_ERR_BAD_STATE);
        }
    }

    // Returns the number of blocks this entry will take up in the journal.
    size_t BlockCount() const {
        if (commit_index_ == header_index_) {
            return 0;
        }

        return block_count_ + kEntryMetadataBlocks;
    }

    bool HasData() const { return !is_dummy_ && BlockCount(); }
    bool is_dummy() const { return is_dummy_; }

    // Generates a sync callback for this entry, which is designed to let the client know when the
    // entry has been fully prepared for writeback.
    ReadyCallback CreateReadyCallback();

    // Generates a sync callback for this entry, which is designed to update the state of the entry
    // after the writeback thread attempts persistence.
    SyncCallback CreateSyncCallback();

    // Update the entry status based on |result|.
    void SetStatusFromResult(zx_status_t result);

    // Set the commit block's checksum.
    void SetChecksum(uint32_t checksum);

    // Return indices of the header and commit block, respectively.
    size_t header_index() const { return header_index_; }
    size_t commit_index() const { return commit_index_; }

    // Return the header and commit blocks of the entry, respectively.
    const HeaderBlock& header_block() const { return header_block_; }
    const CommitBlock& commit_block() const { return commit_block_; }

    // Returns the current status of the entry:
    // - ZX_ERR_ASYNC when waiting for completion of an operation.
    // - ZX_OK when ready to make progress: waiting for the caller to call
    //   Continue().
    // - ZX_ERR_STOP when finished.
    // - Any other value: the error reported by the last operation.
    //
    // The expected flow is for the caller to use Start() and then repeatedly
    // use GetStatus() to decide what to do next.
    zx_status_t GetStatus() const;

    // Starts processing for this entry: run the state machine as far as
    // possible.
    // TODO(rvargas): Remove this method.
    void Start() { DoLoop(ZX_OK); }

    // Moves to the next step on the state machine. Returns the entry state
    // after the state machine cannot make more progress.
    zx_status_t Continue() {
        DoLoop(ZX_OK);
        return GetStatus();
    }

  private:
    // Set of possible next_state_. The name indicates what the entry is waiting
    // for, for example "waiting to write the journal slot".
    enum class State {
        kWriteJournalSlot,
        kWriteJournalSlotComplete,
        kWriteData,
        kWriteDataComplete,
        kDeleteJournalSlot,
        kDeleteJournalSlotComplete,
        kDone,
        kUnset,  // The next state has not been established yet.
    };

    void DoLoop(zx_status_t result);
    zx_status_t DoWriteJournalEntry() __TA_REQUIRES(lock_);
    zx_status_t DoWriteJournalEntryComplete(zx_status_t result) __TA_REQUIRES(lock_);
    zx_status_t DoWriteData() __TA_REQUIRES(lock_);
    zx_status_t DoWriteDataComplete(zx_status_t result) __TA_REQUIRES(lock_);
    zx_status_t DoDeleteEntry() __TA_REQUIRES(lock_);
    zx_status_t DoDeleteEntryComplete(zx_status_t result) __TA_REQUIRES(lock_);
    void TransitionTo(State state) __TA_REQUIRES(lock_);

    void OnOperationComplete(zx_status_t result) {
        DoLoop(result);
    }

    // Returns the WritebackWork this entry represents.
    // Any WritebackWorks acquired via TakeWork() with callbacks referencing the entry must
    // be called while the entry is still alive, as the entry requires the result of these callbacks
    // before moving on to its next state.
    std::unique_ptr<WritebackWork> TakeWork();

    mutable fbl::Mutex lock_;  // Remove once threading is resolved.

    State next_state_ __TA_GUARDED(lock_) = State::kWriteJournalSlot;
    zx_status_t last_status_ __TA_GUARDED(lock_) = ZX_OK;
    bool is_dummy_;  // Journal in read-only mode.

    JournalWriter* journal_;  // Pointer to the journal containing this entry.
    uint32_t block_count_;  // Number of blocks in the entry (not including header/commit).

    // Contents of the start and commit blocks for this journal entry.
    HeaderBlock header_block_;
    CommitBlock commit_block_;

    // Start and commit indices of the entry within the journal vmo in units of blobfs blocks.
    uint64_t header_index_;
    uint64_t commit_index_;

    // WritebackWork for the data contained in this entry.
    std::unique_ptr<WritebackWork> work_;
};


} // blobfs
