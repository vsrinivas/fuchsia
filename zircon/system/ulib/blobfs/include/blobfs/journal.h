// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
static_assert(false, "Fuchsia only header");
#endif

#include <stdint.h>

#include <utility>

#include <blobfs/format.h>
#include <blobfs/journal_entry.h>
#include <blobfs/transaction-manager.h>
#include <blobfs/vmo-buffer.h>
#include <blobfs/writeback.h>
#include <blobfs/writeback-queue.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <zircon/types.h>

namespace blobfs {

class JournalProcessor;

constexpr uint64_t kEntryHeaderMagic = 0x776f7768656c6c6fULL;
constexpr uint64_t kEntryCommitMagic = 0x7472696369612331ULL;

// Transitional interface for the processor. To be removed with the processor.
class JournalForProcessor : public JournalWriter {
  public:
    JournalForProcessor() = default;
    virtual ~JournalForProcessor() = default;

    virtual bool IsReadOnly() const = 0;

    // Shortcut to create a WritebackWork with no associated Blob.
    virtual std::unique_ptr<WritebackWork> CreateWork() = 0;
};

using EntryQueue = fs::Queue<std::unique_ptr<JournalEntry>>;

// Journal which manages the in-memory journal (and background thread, which handles writing
// out entries to the on-disk journal, actual disk locations, and cleaning up old entries).
//
// With journaling enabled, the blobfs writeback flow is as follows:
//
// 1. Once a metadata WritebackWork containing a complete, atomic set of transactions is prepared
//    for writeback, it is enqueued to the Journal. If the WritebackWork contains only a sync
//    callback, then no preparation is done, but it is also sent to the JournalThread. Any entries
//    containing sync callbacks will go through the same queues as regular entries from here on
//    out, but nothing will be done with them until step 7.
//
// 2. The JournalThread will write the transaction data to its buffer, and send work to the
//    WritebackBuffer queue with transactions intended to write the journal entry out to disk.
//    However, the header and commit blocks will not yet be written out to the buffer, so the work
//    will block the writeback queue (not allowing any more writes to go through) until it is ready.
//
// 3. In the JournalThread, the entry whose work has been processed and sent to the
//    writeback queue will have its header and commit blocks written to the buffer, and will then
//    present its work as "ready" to the writeback queue.
//
// 4. Once a journal entry has been written out to disk, the journal will receive a callback to let
//    it know that the entry has been processed. At this point we know it is safe to write the data
//    out to its intended on-disk location.
//
// 5. Once the metadata has been written out to disk, the journal will receive another callback to
//    let it know that we can now "delete" the entry, and free up space for future entries in the
//    journal's buffer.
//
// 6. At this point the journal's info block is updated to reflect the index of the first entry and
//    current known length of all journal entries, the header/commit blocks of all fully processed
//    entries are erased, and any sync works are completed.
//
// 7. Now that all entries/metadata are up to date, we complete any sync requests that have made
//    their way through all the journaling queues.
//
// 8. The JournalThread will continue processing incoming entries until it receives the unmount
//    signal, at which point it will ensure that no entries are still waiting to be processed
//    before exiting.
class Journal : public JournalForProcessor {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Journal);

    // Calls constructor, return an error if anything goes wrong.
    static zx_status_t Create(TransactionManager* transaction_manager, uint64_t block_count,
                              uint64_t start_block, std::unique_ptr<Journal>* out);

    ~Journal();

    // Loads the contents of the journal on disk into the in-memory buffers.
    // This must be called before the journal can be replayed.
    zx_status_t Load();

    // Checks for any existing journal entries starting at the |start| indicated in the super block,
    // and replays all valid entries in order.
    // This method must be called before the journal background thread is initialized.
    zx_status_t Replay();

    // Initializes the journal's background thread.
    zx_status_t InitWriteback();

    // Attempts to enqueue a set of transactions to the journal.
    // An error will be returned if the journal is currently in read only mode.
    zx_status_t Enqueue(std::unique_ptr<WritebackWork> work);

    // Asynchronously processes journal entries and updates journal state.
    void ProcessLoop();

    // JournalWriter interface:
    void ProcessEntryResult(zx_status_t result, JournalEntry* entry) final;
    void WriteEntry(JournalEntry* entry) final;
    void DeleteEntry(JournalEntry* entry) final;
    zx_status_t EnqueueEntryWork(std::unique_ptr<WritebackWork> work) final;

    // JournalForProcessor interface:
    bool IsReadOnly() const final __TA_REQUIRES(lock_) {
        return state_ == WritebackState::kReadOnly;
    }
    std::unique_ptr<WritebackWork> CreateWork() final;

    // Stops the asynchronous queue processor. Returns |ZX_ERR_BAD_STATE| if Teardown() has already
    // been called.
    zx_status_t Teardown();

private:
    // The waiter struct may be used as a stack-allocated queue for producers.
    // It allows them to take turns putting data into the buffer when it is
    // mostly full.
    struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
    using ProducerQueue = fs::Queue<Waiter*>;

    Journal(TransactionManager* transaction_manager, VmoBuffer info,
            std::unique_ptr<Buffer> entries, uint64_t start_block)
        : transaction_manager_(transaction_manager), start_block_(start_block),
          info_(std::move(info)), entries_(std::move(entries)) {}

    bool IsRunning() const __TA_REQUIRES(lock_);

    // Creates an entry within the journal ranging from |header_index| to |commit_index|, inclusive.
    std::unique_ptr<JournalEntry> CreateEntry(uint64_t header_index, uint64_t commit_index,
                                              std::unique_ptr<WritebackWork> work)
                                             __TA_REQUIRES(lock_);

    // Signals the journal thread to process waiting entries,
    // and potentially update the readonly state of the journal.
    void SendSignalLocked(zx_status_t status) __TA_REQUIRES(lock_);

    // Prepares |work| with transactions to write the data for |entry| stored in the journal buffer
    // into the actual journal. This will consist of at most 2 transactions (if we wrap around the
    // end of the circular buffer). The entry in the buffer itself may not be ready at this point.
    void PrepareWork(JournalEntry* entry, std::unique_ptr<WritebackWork>* work);

    // Writes out the header and commit blocks belonging to |entry| to the buffer.
    // All data from |entry| should already be written to the buffer.
    void PrepareBuffer(JournalEntry* entry) __TA_EXCLUDES(lock_);

    // Prepares |entry| for deletion by zeroing out the header and commit block in the buffer,
    // and adds transactions for the deletions to |work|.
    void PrepareDelete(JournalEntry* entry, WritebackWork* work) __TA_EXCLUDES(lock_);

    // Returns the block at |index| within the buffer as a journal entry header block.
    HeaderBlock* GetHeaderBlock(uint64_t index) {
        return reinterpret_cast<HeaderBlock*>(entries_->MutableData(index));
    }

    // Returns the block at |index| within the buffer as a journal entry commit block.
    CommitBlock* GetCommitBlock(uint64_t index) {
        return reinterpret_cast<CommitBlock*>(entries_->MutableData(index));
    }

    // Returns true if the header block at |header_index| and its corresponding commit block
    // describe a valid journal entry. |last_timestamp| is the timestamp of the previous entry,
    // or the journal info block if this is the first entry in the journal.
    // |expect_valid| indicates whether a valid entry is expected to exist here.
    // Meant to be used on Journal replay.
    bool VerifyEntryMetadata(size_t header_index, uint64_t last_timestamp, bool expected_valid)
        __TA_REQUIRES(lock_);

    // Checks if the entry at |header_index| is valid, and if so replays the entry.
    // Takes in |remaining_length| of the journal to determine whether we expect this entry to
    // exist or not. Returns the number of |entry_blocks| and the entry |timestamp|.
    zx_status_t ReplayEntry(size_t header_index, size_t remaining_length, uint64_t* entry_blocks,
                            uint64_t* timestamp) __TA_REQUIRES(lock_);

    // Commits the replay by updating the journal info block and syncing all writes to disk.
    zx_status_t CommitReplay() __TA_REQUIRES(lock_);

    // Updates the journal's info block and persists it to disk.
    zx_status_t WriteInfo(uint64_t start, uint64_t length);

    // Returns data from the info buffer as a JournalInfo block.
    JournalInfo* GetInfo() {
        return reinterpret_cast<JournalInfo*>(info_.MutableData(0));
    }

    // Blocks until |blocks| blocks of data are available within the entries buffer.
    // Doesn't actually allocate any space.
    void EnsureSpaceLocked(size_t blocks) __TA_REQUIRES(lock_);

    // Given a |start| index and |length|, generates one or more transactions
    // from the entries buffer.
    void AddEntryTransaction(size_t start, size_t length, WritebackWork* work);

    // Generates a crc32 checksum for the entry with header block at |header_index|
    // and commit block at |commit_index|.
    uint32_t GenerateChecksum(uint64_t header_index, uint64_t commit_index);

    // Removes and returns the next JournalEntry from the work queue.
    std::unique_ptr<JournalEntry> GetNextEntry() __TA_EXCLUDES(lock_);

    // Processes entries in the work queue and the processor queues.
    void ProcessQueues(JournalProcessor* processor) __TA_EXCLUDES(lock_);

    TransactionManager* transaction_manager_;

    // The absolute start block of the journal on disk. Used for transactions.
    uint64_t start_block_;

    // Signalled when the journal entry buffer has space to add additional entries.
    cnd_t producer_cvar_ = CND_INIT;
    // Signalled when journal entries are ready to be processed by the background thread.
    cnd_t consumer_cvar_ = CND_INIT;

    // Work associated with the "journal" thread, which manages work items (i.e. journal entries),
    // and flushes them to disk. This thread acts as a consumer of the entry buffer.
    thrd_t thread_;

    // Use to lock resources that may be accessed asynchronously.
    fbl::Mutex lock_;

    // This buffer contains the data for the journal info block,
    // which is periodically updated and written back to disk.
    VmoBuffer info_;

    // This buffer contains all journal entry data.
    std::unique_ptr<Buffer> entries_;

    // True if the journal thread has been signalled via the buffer's consumer_cvar_.
    // Reset to false at the beginning of the journal async loop.
    bool consumer_signalled_ __TA_GUARDED(lock_) = false;

    // Used to tell the background thread to exit.
    bool unmounting_ __TA_GUARDED(lock_) = false;

    // The Journal will start off in a kInit state, and will change to kRunning when the
    // background thread is brought up. Once it is running, if an error is detected during
    // writeback, the journal is converted to kReadOnly, and no further writes are permitted.
    WritebackState state_ __TA_GUARDED(lock_) = WritebackState::kInit;

    // Queues which contain journal entries in various states.

    // The work_queue_ contains entries which have been written to the buffer,
    // but not yet persisted to the journal on disk.
    EntryQueue work_queue_ __TA_GUARDED(lock_);

    // Ensures that if multiple producers are waiting for space to write their
    // entries into the entry buffer, they can each write in-order.
    ProducerQueue producer_queue_ __TA_GUARDED(lock_);

    JournalProcessor* processor_ = nullptr;
};

// Result returned from a JournalProcessor's Process methods.
enum class ProcessResult {
    kContinue, // Indicates that the entry should be added to the next queue.
    kWait, // Indicates that we should wait before processing this entry.
    kRemove, // Indicates that the entry should be removed from the queue.
};

// The JournalProcessor is used in the context of the Journal async thread to process entries in
// different states. Entries from the Journal's work queue are processed first, then go through the
// wait, delete, and potentially sync queues (if a sync callback is present). Process operations
// are expected to be called in that order. Based on the state of the journal and the entry itself,
// different actions may be taken at each step in the process.
class JournalProcessor {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(JournalProcessor);

    JournalProcessor() = delete;

    explicit JournalProcessor(JournalForProcessor* journal)
            : journal_(journal), error_(journal->IsReadOnly()), blocks_processed_(0) {}

    ~JournalProcessor() {
        ZX_ASSERT(IsEmpty());
    }

    void ProcessWorkEntry(std::unique_ptr<JournalEntry> entry);
    void ProcessWaitQueue();
    void ProcessDeleteQueue();

    bool HasError() const { return error_; }

    bool IsEmpty() const {
        return wait_queue_.is_empty() && delete_queue_.is_empty();
    }

    void ResetWork() {
        if (work_ != nullptr) {
            // WritebackWork must be marked complete here to avoid failing the assertion that
            // pending write requests do not still exist on WriteTxn destruction.
            work_->MarkCompleted(ZX_ERR_BAD_STATE);
            work_.reset();
        }
    }

    void EnqueueWork() {
        if (work_ != nullptr) {
            journal_->EnqueueEntryWork(std::move(work_));
        }
    }

    size_t GetBlocksProcessed() const { return blocks_processed_; }
    void AddBlocks(size_t num_blocks) { blocks_processed_ += num_blocks; }

private:
    void ProcessQueue(EntryQueue* in_queue, EntryQueue* out_queue);
    ProcessResult ProcessEntry(JournalEntry* entry);

    JournalForProcessor* journal_;
    bool error_;
    std::unique_ptr<WritebackWork> work_;
    size_t blocks_processed_;

    // Queues which track the state of the journal entries.

    // The wait_queue_ contains entries which have been persisted to the journal,
    // but not yet persisted to the final on-disk location.
    EntryQueue wait_queue_;

    // The delete_queue_ contains entries which have been fully persisted to disk,
    // but not yet removed from the journal.
    EntryQueue delete_queue_;
};

} // blobfs
