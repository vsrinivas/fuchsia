// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_JOURNAL_H_
#define FS_JOURNAL_JOURNAL_H_

#include <lib/fit/barrier.h>
#include <lib/fit/promise.h>
#include <lib/fit/sequencer.h>
#include <lib/inspect/cpp/inspect.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>

#include <cobalt-client/cpp/collector.h>
#include <fbl/vector.h>
#include <fs/journal/background_executor.h>
#include <fs/journal/format.h>
#include <fs/journal/internal/journal_writer.h>
#include <fs/journal/internal/metrics.h>
#include <fs/journal/superblock.h>
#include <fs/metrics/events.h>
#include <fs/transaction/transaction_handler.h>
#include <storage/buffer/blocking_ring_buffer.h>
#include <storage/buffer/ring_buffer.h>
#include <storage/operation/unbuffered_operation.h>

namespace fs {

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
//      Journal journal(...);
//      auto data_promise = journal.WriteData(vnode_data);
//      journal.CommitTransaction({
//        .metadata_operations = ...
//      });
//
//      // A few moments later...
//
//      journal.schedule_task(Sync().and_then([]() {
//          printf("Operation completed successfully!");
//      }));
//
// This class is thread-safe.
class Journal final : public fit::executor {
 public:
  using Promise = fit::promise<void, zx_status_t>;

  struct Options {
    // Pointer to MetricsTrait that helps journal maintain metrics.
    // The reference to MetricsTrait is dropped when Journal object is destroyed.
    // A nullptr implies that the user doesn't use/want journal metrics.
    std::shared_ptr<MetricsTrait> metrics;
  };

  struct Transaction {
    // Mandatory; for now, there is no support for data/trim-only transactions.
    std::vector<storage::UnbufferedOperation> metadata_operations;

    // Optional promise responsible for writing the data.
    Promise data_promise;

    // Optional trim operations.
    std::vector<storage::BufferedOperation> trim;

    // Called when metadata has been written and *flushed* to the journal.  At that point, any
    // subsequent writes (which may or may not involve the journal) are guaranteed to be visible
    // *after* this transaction.  The primary use case for this is to keep blocks that are freed in
    // a transaction as reserved until the transaction is guaranteed to be visible, since prior to
    // that, the old data needs to be preserved.  Optional.
    fit::callback<void()> commit_callback;

    // Called when metadata has been written to its final location (but not necessarily flushed), so
    // this is after |commit_callback|.  Reads to the device after this callback will then return
    // metadata from this transaction.  Optional.
    fit::callback<void()> complete_callback;
  };

  // Constructs a Journal with journaling enabled. This is the traditional constructor
  // of Journals, where data and metadata are treated separately.
  //
  // |journal_superblock| represents the journal info block.
  // |journal_buffer| must be the size of the entries (not including the info block).
  // |journal_start_block| must point to the start of the journal info block.
  Journal(fs::TransactionHandler* transaction_handler, JournalSuperblock journal_superblock,
          std::unique_ptr<storage::BlockingRingBuffer> journal_buffer,
          std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer,
          uint64_t journal_start_block, Options options);

  // Synchronizes with the background thread to ensure all enqueued work is complete before
  // returning.
  ~Journal() final;

  // Transmits operations containing pure data, which may be subject to different atomicity
  // guarantees than metadata updates.
  //
  // Multiple requests to WriteData are not ordered. If ordering is desired, it should
  // be added using a |fit::sequencer| object, or by chaining the data writeback promise
  // along an object which is ordered.
  Promise WriteData(std::vector<storage::UnbufferedOperation> operations);

  // Commits a transaction.
  [[nodiscard]] zx_status_t CommitTransaction(Transaction transaction);

  // Returns a promise which identifies that all previous committed transcations have completed
  // (regardless of success).  Additionally, prompt the internal journal writer to update the info
  // block, if it isn't already up-to-date.
  Promise Sync();

  // Schedules a promise to the journals background thread executor.
  void schedule_task(fit::pending_task task) final {
    auto event = metrics_->NewLatencyEvent(fs_metrics::Event::kJournalScheduleTask);
    executor_.schedule_task(std::move(task));
  }

  // See comment below for write_metadata_callback_ for how this might be used.
  void set_write_metadata_callback(fit::function<void()> callback) {
    write_metadata_callback_ = std::move(callback);
  }

  // Returns true if all writeback is "off", and no further data will be written to the
  // device.
  bool IsWritebackEnabled() const { return writer_.IsWritebackEnabled(); }

 private:
  JournalMetrics* metrics() { return metrics_.get(); }

  // Flushes blocks that have been delayed until we can flush.  See related |pending_ below.
  void FlushPending();

  std::unique_ptr<storage::BlockingRingBuffer> journal_buffer_;
  std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer_;

  // The number of blocks stuck in the journal buffer that are blocked until we decide to flush.
  // This does not include blocks that are in the buffer but currently, or soon will be, in-flight
  // i.e.  there can be blocks reserved in the journal buffers that are not tracked by this counter,
  // but the process to flush them has been initiated and it is just a matter of time before they
  // become available.
  size_t pending_ = 0;

  // Barrier for all outstanding data writes.
  fit::barrier data_barrier_;

  // The journal must enforce the requirement that metadata operations are completed in
  // the order they are enqueued. To fulfill this requirement, a sequencer guarantees
  // ordering of internal promise structures before they are handed to |executor_|.
  fit::sequencer journal_sequencer_;

  // A promise that we use to block writes to the journal until data writes have been flushed.
  fit::promise<> journal_data_barrier_;

  // Journal metrics. This metrics is shared with JournalWriter and potentially other threads.
  // The reference to MetricsTrait is dropped when Journal object is destroyed.
  std::shared_ptr<JournalMetrics> metrics_;

  internal::JournalWriter writer_;

  // Intentionally place the executor at the end of the journal. This ensures that
  // during destruction, the executor can complete pending tasks operation on the writeback
  // buffers before the writeback buffers are destroyed.
  BackgroundExecutor executor_;

  const Options options_;

  // The callback will be called synchronously after metadata has been submitted to the underlying
  // device. This is after both the writes to the journal ring buffer and the actual metadata
  // resting place. This can be used, for example, to perform an fsck at the end of every
  // transaction (for testing purposes).
  fit::function<void()> write_metadata_callback_;
};

}  // namespace fs

#endif  // FS_JOURNAL_JOURNAL_H_
