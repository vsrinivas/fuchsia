// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
static_assert(false, "Fuchsia only header");
#endif

#include <zircon/types.h>

#include <stdint.h>

#include <blobfs/format.h>
#include <blobfs/writeback.h>

namespace blobfs {
class Journal;
class JournalProcessor;

using ReadyCallback = blobfs::WritebackWork::ReadyCallback;
using SyncCallback = fs::Vnode::SyncCallback;

enum class EntryStatus : uint32_t {
    kSync,      // State given to a journal entry which represents a sync request.
    kInit,      // State given to a journal entry which requires additional pre-processing.
    kWaiting,   // State given to an entry which is waiting for writeback to complete.
    kPersisted, // State given to an entry which has been successfully persisted to disk.
    kError,     // State given to an entry which has encountered an error during writeback.
};

constexpr uint64_t kEntryHeaderMagic = 0x776f7768656c6c6fULL;
constexpr uint64_t kEntryCommitMagic = 0x7472696369612331ULL;

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

    JournalEntry(Journal* journal, EntryStatus status, uint64_t header_index, uint64_t commit_index,
                 fbl::unique_ptr<WritebackWork> work);

    // Forcibly resets the associated WritebackWork. This should only be called in the event of an
    // error; i.e. blobfs has transitioned to a readonly state. This reset should also resolve any
    // pending sync closures within the work.
    void ForceReset() {
        if (work_ != nullptr) {
            work_->Reset(ZX_ERR_BAD_STATE);
        }
    }

    // Returns the number of blocks this entry will take up in the journal.
    size_t BlockCount() {
        if (commit_index_ == header_index_) {
            return 0;
        }

        return block_count_ + kEntryMetadataBlocks;
    }

    // Returns the WritebackWork this entry represents.
    // Any WritebackWorks acquired via TakeWork() with callbacks referencing the entry must
    // be called while the entry is still alive, as the entry requires the result of these callbacks
    // before moving on to its next state.
    fbl::unique_ptr<WritebackWork> TakeWork();

    // Generates a sync callback for this entry, which is designed to let the client know when the
    // entry has been fully prepared for writeback.
    ReadyCallback CreateReadyCallback();

    // Generates a sync callback for this entry, which is designed to update the state of the entry
    // after the writeback thread attempts persistence.
    SyncCallback CreateSyncCallback();

    // Returns the current status.
    // When the status is "kWaiting", we are waiting on another thread to change the state of the
    // entry. Once the state is changed from kWaiting, we are guaranteed that it will not be
    // changed again from an external thread.
    // The one exception to this is if an entry is in the kInit state, meaning that it is waiting
    // on the journal thread to calculate the checksum, etc. However, it is waiting in the
    // writeback thread at this time, so if another error is encountered it may be set to kError
    // before the journal thread can set it to kWaiting.
    EntryStatus GetStatus() const {
        return static_cast<EntryStatus>(status_.load());
    }

    // Sets status_ to |status| and returns the previous status.
    EntryStatus SetStatus(EntryStatus status) {
        return static_cast<EntryStatus>(status_.exchange(static_cast<uint32_t>(status)));
    }

    // Set the commit block's checksum.
    void SetChecksum(uint32_t checksum);

    // Return indices of the header and commit block, respectively.
    size_t GetHeaderIndex() const { return header_index_; }
    size_t GetCommitIndex() const { return commit_index_; }

    // Return the header and commit blocks of the entry, respectively.
    const HeaderBlock& GetHeaderBlock() const { return header_block_; }
    const CommitBlock& GetCommitBlock() const { return commit_block_; }

private:
    Journal* journal_; // Pointer to the journal containing this entry.
    fbl::atomic<uint32_t> status_; // Current EntryStatus. Accessed by multiple threads.
    uint32_t block_count_; // Number of blocks in the entry (not including header/commit).

    // Contents of the start and commit blocks for this journal entry.
    HeaderBlock header_block_;
    CommitBlock commit_block_;

    // Start and commit indices of the entry within the journal vmo in units of blobfs blocks.
    uint64_t header_index_;
    uint64_t commit_index_;

    // WritebackWork for the data contained in this entry.
    fbl::unique_ptr<WritebackWork> work_;
};

using EntryQueue = fs::Queue<fbl::unique_ptr<JournalEntry>>;

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
class Journal {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Journal);

    // Calls constructor, return an error if anything goes wrong.
    static zx_status_t Create(Blobfs* bs, uint64_t block_count, uint64_t start_block,
                              fbl::unique_ptr<Journal>* out);

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
    zx_status_t Enqueue(fbl::unique_ptr<WritebackWork> work);

    // Signals the journal thread to process waiting entries.
    void SendSignal(zx_status_t status) {
        fbl::AutoLock lock(&lock_);
        SendSignalLocked(status);
    }

    // Asynchronously processes journal entries and updates journal state.
    void ProcessLoop();

private:
    // The waiter struct may be used as a stack-allocated queue for producers.
    // It allows them to take turns putting data into the buffer when it is
    // mostly full.
    struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
    using ProducerQueue = fs::Queue<Waiter*>;

    friend class JournalProcessor;

    Journal(Blobfs* blobfs, fbl::unique_ptr<Buffer> info, fbl::unique_ptr<Buffer> entries,
            uint64_t start_block)
        : blobfs_(blobfs), start_block_(start_block),
          info_(fbl::move(info)), entries_(fbl::move(entries)) {}

    // Creates an entry within the journal ranging from |header_index| to |commit_index|, inclusive.
    fbl::unique_ptr<JournalEntry> CreateEntry(uint64_t header_index, uint64_t commit_index,
                                              fbl::unique_ptr<WritebackWork> work)
                                             __TA_REQUIRES(lock_);

    // Signals the journal thread to process waiting entries,
    // and potentially update the readonly state of the journal.
    void SendSignalLocked(zx_status_t status) __TA_REQUIRES(lock_);

    // Prepares |work| with transactions to write the data for |entry| stored in the journal buffer
    // into the actual journal. This will consist of at most 2 transactions (if we wrap around the
    // end of the circular buffer). The entry in the buffer itself may not be ready at this point.
    void PrepareWork(JournalEntry* entry, fbl::unique_ptr<WritebackWork>* work);

    // Writes out the header and commit blocks belonging to |entry| to the buffer.
    // All data from |entry| should already be written to the buffer.
    void PrepareBuffer(JournalEntry* entry) __TA_EXCLUDES(lock_);

    // Prepares |entry| for deletion by zeroing out the header and commit block in the buffer,
    // and adds transactions for the deletions to |work|.
    void PrepareDelete(JournalEntry* entry, WritebackWork* work) __TA_EXCLUDES(lock_);

    // Shortcut to create a WritebackWork with no associated VnodeBlob.
    fbl::unique_ptr<WritebackWork> CreateWork();

    // Enqueues transactions from the entry buffer to the blobfs writeback queue.
    // Verifies the transactions and sets the buffer if necessary.
    zx_status_t EnqueueEntryWork(fbl::unique_ptr<WritebackWork> work);

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
        return reinterpret_cast<JournalInfo*>(info_->MutableData(0));
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

    bool IsReadOnly() const __TA_REQUIRES(lock_) { return state_ == WritebackState::kReadOnly; }

    // Removes and returns the next JournalEntry from the work queue.
    fbl::unique_ptr<JournalEntry> GetNextEntry() __TA_EXCLUDES(lock_);

    // Processes entries in the work queue and the processor queues.
    void ProcessQueues(JournalProcessor* processor) __TA_EXCLUDES(lock_);

    Blobfs* blobfs_;

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
    fbl::unique_ptr<Buffer> info_;

    // This buffer contains all journal entry data.
    fbl::unique_ptr<Buffer> entries_;

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
};

// Result returned from a JournalProcessor's Process methods.
enum class ProcessResult {
    kContinue, // Indicates that the entry should be added to the next queue.
    kWait, // Indicates that we should wait before processing this entry.
    kRemove, // Indicates that the entry should be removed from the queue.
};

// Profile to track which queue the JournalProcessor is currently handling.
enum class ProcessorContext {
    kWork,
    kWait,
    kDelete,
    kSync,
    kDefault,
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

    explicit JournalProcessor(Journal* journal) : journal_(journal),
                                                  error_(journal->IsReadOnly()),
                                                  blocks_processed_(0),
                                                  context_(ProcessorContext::kDefault) {}

    ~JournalProcessor() {
        SetContext(ProcessorContext::kDefault);
    }

    void ProcessWorkEntry(fbl::unique_ptr<JournalEntry> entry);
    void ProcessWaitQueue();
    void ProcessDeleteQueue();
    void ProcessSyncQueue();

    bool HasError() const { return error_; }

    bool IsEmpty() const {
        return wait_queue_.is_empty() && delete_queue_.is_empty() && sync_queue_.is_empty();
    }

    void ResetWork() {
        if (work_ != nullptr) {
            work_->Reset(ZX_ERR_BAD_STATE);
        }
    }

    void EnqueueWork() {
        if (work_ != nullptr) {
            journal_->EnqueueEntryWork(fbl::move(work_));
        }
    }

    size_t GetBlocksProcessed() const { return blocks_processed_; }
private:
    // Sets queue type being processed to |context|.
    void SetContext(ProcessorContext context);

    void ProcessQueue(EntryQueue* in_queue, EntryQueue* out_queue);
    ProcessResult ProcessEntry(JournalEntry* entry);

    // Methods which process entries in different ways.
    // Default action to take for a valid "work" |entry|.
    ProcessResult ProcessWorkDefault(JournalEntry* entry);

    // Default action to take for a valid "wait" |entry|.
    ProcessResult ProcessWaitDefault(JournalEntry* entry);

    // Default action to take for a valid "delete" |entry|.
    ProcessResult ProcessDeleteDefault(JournalEntry* entry);

    // Default action to take for a "sync" |entry|.
    ProcessResult ProcessSyncDefault(JournalEntry* entry);

    // Action to take for a "sync" |entry| when we want to complete the sync operation.
    ProcessResult ProcessSyncComplete(JournalEntry* entry);

    // Default action to take when we find an |entry| with "error" status.
    ProcessResult ProcessErrorDefault();

    // Action to take when we find an error entry and want to remove it from the queue.
    ProcessResult ProcessErrorComplete(JournalEntry* entry);

    // Action to take for an unsupported state.
    ProcessResult ProcessUnsupported();

    Journal* journal_;
    bool error_;
    fbl::unique_ptr<WritebackWork> work_;
    size_t blocks_processed_;

    // Queue type that the Processor is currently processing.
    ProcessorContext context_;

    // Queues which track the state of the journal entries.

    // The wait_queue_ contains entries which have been persisted to the journal,
    // but not yet persisted to the final on-disk location.
    EntryQueue wait_queue_;

    // The delete_queue_ contains entries which have been fully persisted to disk,
    // but not yet removed from the journal.
    EntryQueue delete_queue_;

    // Stores any sync works pulled from the delete queue in this sync queue, so we can
    // complete them after we update the journal's info block.
    EntryQueue sync_queue_;
};
} // blobfs
