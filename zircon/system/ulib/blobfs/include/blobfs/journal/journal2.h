// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_JOURNAL_JOURNAL2_H_
#define BLOBFS_JOURNAL_JOURNAL2_H_

#include <lib/fit/barrier.h>
#include <lib/fit/promise.h>
#include <lib/fit/sequencer.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <blobfs/background-executor.h>
#include <blobfs/blocking-ring-buffer.h>
#include <blobfs/format.h>
#include <blobfs/journal/superblock.h>
#include <blobfs/operation.h>
#include <blobfs/ring-buffer.h>
#include <blobfs/writeback.h>
#include <fbl/vector.h>
#include <fs/block-txn.h>

namespace blobfs {
namespace internal {

// A small container encapsulating a buffered request, along with the reservation that makes the
// buffered request valid. These two pieces of data are coupled together because lifetime of the
// operations must not exceed the lifetime of the reservation itself.
//
// This struct is used for both journaled metadata and unjournaled data.
struct JournalWorkItem {
  JournalWorkItem(BlockingRingBufferReservation reservation,
                  fbl::Vector<BufferedOperation> operations)
      : reservation(std::move(reservation)), operations(std::move(operations)) {}

  BlockingRingBufferReservation reservation;
  fbl::Vector<BufferedOperation> operations;
};

// The back-end of the journal. This class implements all the blocking operations which transmit
// buffers to disk, without providing higher-level guarantees about asynchrony or execution
// ordering.
//
// This class is thread-compatible.
// This class is not movable or copyable.
class JournalWriter {
 public:
  JournalWriter(fs::TransactionHandler* transaction_handler, JournalSuperblock journal_superblock,
                uint64_t journal_start_block, uint64_t entries_length);
  explicit JournalWriter(fs::TransactionHandler* transaction_handler);
  JournalWriter(const JournalWriter&) = delete;
  JournalWriter& operator=(const JournalWriter&) = delete;
  JournalWriter(JournalWriter&& other) = delete;
  JournalWriter& operator=(JournalWriter&& other) = delete;

  // Writes |work| to disk immediately.
  fit::result<void, zx_status_t> WriteData(JournalWorkItem work);

  // Writes |work| to disk immediately (possibly also to the journal)
  // Precondition: |block_count| is the number of blocks modified by |work|.
  //
  // Updating metadata has three phases:
  // 1) Updating the info block (if necessary to make space)
  // 2) Write metadata to the journal itself.
  // 3) Write metadata to the final on-disk location.
  //
  // This method currently blocks, completing all three phases before returning, but in the future,
  // could be more fine grained, returning a promise that represents the completion of all phases.
  fit::result<void, zx_status_t> WriteMetadata(JournalWorkItem work, uint64_t block_count);

  // Synchronizes the most up-to-date info block back to disk.
  //
  // Returns ZX_ERR_IO_REFUSED if writeback is disabled.
  // Returns an error from the block device if the info block cannot be written.
  // In all other cases, returns ZX_OK.
  fit::result<void, zx_status_t> Sync();

  // Returns true if journaling is "on", and metadata is treated differently
  // from regular data.
  //
  // This method is thread-safe.
  [[nodiscard]] bool IsJournalingEnabled() const { return entries_length_ != 0; }

 private:
  // Returns true if all writeback is "off", and no further data will be written to the
  // device.
  [[nodiscard]] bool IsWritebackEnabled() const { return transaction_handler_; }

  // Deactivates all writeback, calling all subsequent write operations to fail.
  void DisableWriteback() { transaction_handler_ = nullptr; }

  // Returns the start of the portion of the journal which stores metadata.
  [[nodiscard]] uint64_t InfoStartBlock() const { return journal_start_block_; }

  // Returns the length of the portion of the journal which stores metadata.
  [[nodiscard]] static uint64_t InfoLength() { return kJournalMetadataBlocks; }

  // Returns the start of the portion of the journal which stores entries.
  [[nodiscard]] uint64_t EntriesStartBlock() const {
    return journal_start_block_ + kJournalMetadataBlocks;
  }

  // Returns the length of the portion of the journal which stores entries.
  [[nodiscard]] uint64_t EntriesLength() const { return entries_length_; }

  // Writes |work| to the journal, and flushes it to the underlying device.
  //
  // Blocks the calling thread on I/O until the operation completes.
  zx_status_t WriteMetadataToJournal(JournalWorkItem* work, uint64_t block_count);

  // Writes the info block if adding a |block_count| block entry to the journal
  // will hit the start of the journal.
  zx_status_t WriteInfoBlockIfIntersect(uint64_t block_count);

  // Writes the info block to the underlying device.
  // Asserts that the sequence number has increased, and that the info block has
  // a meaningful update.
  //
  // Blocks the calling thread on I/O until the operation completes.
  zx_status_t WriteInfoBlock();

  // Writes operations directly through to disk.
  //
  // If any operations fail, this method will return the resulting error from the underlying
  // block device. Afterwards, however, this function will exclusively return |ZX_ERR_IO_REFUSED|
  // to prevent "partial operations" from being written to the underlying device.
  zx_status_t WriteOperations(const fbl::Vector<BufferedOperation>& operations);

  fs::TransactionHandler* transaction_handler_ = nullptr;
  JournalSuperblock journal_superblock_;
  // Relative to the start of the filesystem. Points to the journal info block.
  uint64_t journal_start_block_ = 0;
  // The value of the sequence_number to be used in the next entry which is written to the
  // journal.
  uint64_t next_sequence_number_ = 0;
  // Relative to |kJournalMetadataBlocks| (the start of entries).
  uint64_t next_entry_start_block_ = 0;
  const uint64_t entries_length_ = 0;
};

}  // namespace internal

// This class implements an interface for filesystems to write back data to the underlying
// device. It provides methods for the following functionality:
// - Writing data to the underlying device
// - Writing metadata to the underlying device (journaled or unjournaled)
// - Revoking metadata from the journal
//
// The journal operates on asynchronous objects: it returns promises corresponding to
// each operation, which may be chained together by the caller, and which may be completed
// by scheduling these promises on the journal's executor via |journal.schedule_task|.
//
// EXAMPLE USAGE
//
//      Journal2 journal(...);
//      auto data_promise = journal.WriteData(vnode_data);
//      auto metadata_promise = journal.WriteMetadata(vnode_metadata);
//      journal.schedule_task(data_promise.and_then(metadata_promise));
//
//      // A few moments later...
//
//      journal.schedule_task(Sync().and_then([]() {
//          printf("Operation completed successfully!");
//      }));
//
// This class is thread-safe.
class Journal2 final : public fit::executor {
 public:
  using Promise = fit::promise<void, zx_status_t>;

  // Constructs a Journal with journaling enabled. This is the traditional constructor
  // of Journals, where data and metadata are treated separately.
  Journal2(fs::TransactionHandler* transaction_handler, JournalSuperblock journal_superblock,
           std::unique_ptr<BlockingRingBuffer> journal_buffer,
           std::unique_ptr<BlockingRingBuffer> writeback_buffer, uint64_t journal_start_block);

  // Constructs a journal where metadata and data are both treated as data, effectively
  // disabling the journal.
  Journal2(fs::TransactionHandler* transaction_handler,
           std::unique_ptr<BlockingRingBuffer> writeback_buffer);

  ~Journal2() final;

  // Transmits operations containing pure data, which may be subject to different atomicity
  // guarantees than metadata updates.
  //
  // Multiple requests to WriteData are not ordered. If ordering is desired, it should
  // be added using a |fit::sequencer| object, or by chaining the data writeback promise
  // along an object which is ordered.
  Promise WriteData(fbl::Vector<UnbufferedOperation> operations);

  // Transmits operations contains metadata, which must be updated atomically with respect
  // to power failures if journaling is enabled.
  //
  // Multiple requests to WriteMetadata are ordered. They are ordered by the invocation
  // of the |WriteMetadata| method, not by the completion of the returned promise.
  Promise WriteMetadata(fbl::Vector<UnbufferedOperation> operations);

  // Identifies that a piece of metadata is no longer being used as metadata.
  //
  // This entry is essential for:
  //   - Journaling modes which exclusively journal metadata, but not data.
  //   - Filesystems which share storage between metadata and data.
  //
  // These entries prevent replaying "old" metadata on top of user data.
  //
  // For example, suppose the following scenario occurs:
  //   - Allocate N, Write block N (metadata).
  //      Journal: [N Allocation] [N Write]
  //   - Free block N.
  //      Journal: [N Allocation] [N Write] [N free]
  //   - Allocate N, Write block N (data)
  //      Journal: [N Allocation] [N Write] [N free] [N Allocation]
  // On replay, the journal would correctly allocate N, free it, and re-allocate
  // it as user data. However, in this process, the journal would also overwrite N
  // with the stale copy of metadata, resulting in "valid metadata, but invalid user data".
  //
  // TODO(ZX-4752): Currently only returns a promise that results in ZX_ERR_NOT_SUPPORTED.
  Promise WriteRevocation(fbl::Vector<Operation> operations);

  // Returns a promise which identifies that all previous promises returned from the journal
  // have completed (succeeded, failed, or abandoned).
  // Additionally, prompt the internal journal writer to update the info block, if it
  // isn't already up-to-date.
  //
  // This promise completes when the promises from all prior invocations of:
  // - WriteData
  // - WriteMetadata
  // - WriteRevocation
  // - Sync
  // Have completed (either successfully or with an error).
  Promise Sync();

  // Schedules a promise to the journals background thread executor.
  void schedule_task(fit::pending_task task) final { executor_.schedule_task(std::move(task)); }

 private:
  std::unique_ptr<BlockingRingBuffer> journal_buffer_;
  std::unique_ptr<BlockingRingBuffer> writeback_buffer_;

  // To implement |Sync()|, the journal must track all pending work, with the ability
  // to react once all prior work (up to a point) has finished execution.
  // This barrier enables a journal to generate new promises identifying when all prior
  // tasks transmitted to |executor_| have completed.
  fit::barrier barrier_;

  // The journal must enforce the requirement that metadata operations are completed in
  // the order they are enqueued. To fulfill this requirement, a sequencer guarantees
  // ordering of internal promise structures before they are handed to |executor_|.
  fit::sequencer metadata_sequencer_;

  internal::JournalWriter writer_;

  // Intentionally place the executor at the end of the journal. This ensures that
  // during destruction, the executor can complete pending tasks operation on the writeback
  // buffers before the writeback buffers are destroyed.
  BackgroundExecutor executor_;
};

}  // namespace blobfs

#endif  // BLOBFS_JOURNAL_JOURNAL2_H_
