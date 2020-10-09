// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_INTERNAL_JOURNAL_WRITER_H_
#define FS_JOURNAL_INTERNAL_JOURNAL_WRITER_H_

#include <lib/fit/result.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>

#include <fbl/vector.h>
#include <fs/journal/format.h>
#include <fs/journal/internal/metrics.h>
#include <fs/journal/internal/operation_tracker.h>
#include <fs/journal/superblock.h>
#include <fs/transaction/transaction_handler.h>
#include <storage/buffer/blocking_ring_buffer.h>
#include <storage/operation/operation.h>

namespace fs {
namespace internal {

// A small container encapsulating a buffered request, along with the reservation that makes the
// buffered request valid. These two pieces of data are coupled together because lifetime of the
// operations must not exceed the lifetime of the reservation itself.
//
// This struct is used for both journaled metadata and unjournaled data.
struct JournalWorkItem {
  JournalWorkItem(storage::BlockingRingBufferReservation reservation,
                  std::vector<storage::BufferedOperation> operations)
      : reservation(std::move(reservation)), operations(std::move(operations)) {}

  storage::BlockingRingBufferReservation reservation;
  std::vector<storage::BufferedOperation> operations;
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
                uint64_t journal_start_block, uint64_t entries_length,
                std::shared_ptr<JournalMetrics> metrics);
  explicit JournalWriter(fs::TransactionHandler* transaction_handler,
                         std::shared_ptr<JournalMetrics> metrics);
  JournalWriter(const JournalWriter&) = delete;
  JournalWriter& operator=(const JournalWriter&) = delete;
  JournalWriter(JournalWriter&& other) = delete;
  JournalWriter& operator=(JournalWriter&& other) = delete;

  // Writes |work| to disk immediately.
  fit::result<void, zx_status_t> WriteData(JournalWorkItem work);

  // Writes |work| to disk immediately (possibly also to the journal)
  //
  // Updating metadata has three phases:
  // 1) Updating the info block (if necessary to make space)
  // 2) Write metadata to the journal itself.
  // 3) Write metadata to the final on-disk location.
  //
  // This method currently blocks, completing all three phases before returning, but in the future,
  // could be more fine grained, returning a promise that represents the completion of all phases.
  fit::result<void, zx_status_t> WriteMetadata(JournalWorkItem work);

  // Trims |operations| immediately.
  fit::result<void, zx_status_t> TrimData(std::vector<storage::BufferedOperation> operations);

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

  // Returns true if all writeback is "off", and no further data will be written to the
  // device.
  [[nodiscard]] bool IsWritebackEnabled() const { return transaction_handler_; }

 private:
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
  zx_status_t WriteMetadataToJournal(JournalWorkItem* work);

  // Writes the info block if adding a |block_count| block entry to the journal
  // will hit the start of the journal.
  zx_status_t WriteInfoBlockIfIntersect(uint64_t block_count);

  // Writes the info block to the underlying device.
  // Asserts that the sequence number has increased, and that the info block has
  // a meaningful update.
  //
  // Blocks the calling thread on I/O until the operation completes.
  zx_status_t WriteInfoBlock();

  // Writes an operation into the journal, creating a sequence of operations
  // which deal with wraparound of the in-memory |reservation| buffer and the on-disk
  // journal. Additionally, issues these operations to the underlying device and
  // returns the result (see |WriteOperations|).
  zx_status_t WriteOperationToJournal(const storage::BlockBufferView& view);

  // Writes operations directly through to disk.
  //
  // If any operations fail, this method will return the resulting error from the underlying
  // block device. Afterwards, however, this function will exclusively return |ZX_ERR_IO_REFUSED|
  // to prevent "partial operations" from being written to the underlying device.
  zx_status_t WriteOperations(const std::vector<storage::BufferedOperation>& operations);

  JournalMetrics* metrics() const { return metrics_.get(); }

  fs::TransactionHandler* transaction_handler_ = nullptr;
  JournalSuperblock journal_superblock_;
  // Tracks all in-flight metadata operations.
  // These operations are tracked from the moment they are written to the journal,
  // and are dropped once the journal would avoid replaying them on reboot.
  OperationTracker live_metadata_operations_;

  // Journal metrics are shared with other journal threads.
  std::shared_ptr<JournalMetrics> metrics_;
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
}  // namespace fs

#endif  // FS_JOURNAL_INTERNAL_JOURNAL_WRITER_H_
