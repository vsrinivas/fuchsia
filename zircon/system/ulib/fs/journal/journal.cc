// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <fs/journal/journal.h>
#include <fs/trace.h>
#include <fs/transaction/writeback.h>

#include "entry_view.h"
#include "format_assertions.h"

namespace fs {
namespace {

zx_status_t CheckAllWriteOperations(const fbl::Vector<storage::UnbufferedOperation>& operations) {
  for (const auto& operation : operations) {
    if (operation.op.type != storage::OperationType::kWrite) {
      FS_TRACE_ERROR("journal: Transmitted non-write operation to writeback\n");
      return ZX_ERR_WRONG_TYPE;
    }
  }
  return ZX_OK;
}

zx_status_t CheckAllTrimOperations(const fbl::Vector<storage::BufferedOperation>& operations) {
  for (const auto& operation : operations) {
    if (operation.op.type != storage::OperationType::kTrim) {
      return ZX_ERR_WRONG_TYPE;
    }
  }
  return ZX_OK;
}

fit::result<void, zx_status_t> SignalSyncComplete(sync_completion_t* completion) {
  FS_TRACE_DEBUG("SignalSyncComplete\n");
  sync_completion_signal(completion);
  return fit::ok();
}

}  // namespace

Journal::Journal(TransactionHandler* transaction_handler, JournalSuperblock journal_superblock,
                 std::unique_ptr<storage::BlockingRingBuffer> journal_buffer,
                 std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer,
                 uint64_t journal_start_block)
    : journal_buffer_(std::move(journal_buffer)),
      writeback_buffer_(std::move(writeback_buffer)),
      writer_(transaction_handler, std::move(journal_superblock), journal_start_block,
              journal_buffer_->capacity()) {}

Journal::Journal(TransactionHandler* transaction_handler,
                 std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer)
    : writeback_buffer_(std::move(writeback_buffer)), writer_(transaction_handler) {}

Journal::~Journal() {
  sync_completion_t completion;
  schedule_task(Sync().then([&completion](const fit::result<void, zx_status_t>& result) {
    return SignalSyncComplete(&completion);
  }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

Journal::Promise Journal::WriteData(fbl::Vector<storage::UnbufferedOperation> operations) {
  zx_status_t status = CheckAllWriteOperations(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Not all operations to WriteData are writes\n");
    return fit::make_error_promise(status);
  }

  // Ensure there is enough space in the writeback buffer.
  uint64_t block_count = BlockCount(operations);
  storage::BlockingRingBufferReservation reservation;
  status = writeback_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to reserve space in writeback buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the buffer.
  fbl::Vector<storage::BufferedOperation> buffered_operations;
  status = reservation.CopyRequests(operations, 0, &buffered_operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to copy operations into writeback buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the data operations to the device.
  auto promise =
      fit::make_promise([this, work = std::move(work)]() mutable -> fit::result<void, zx_status_t> {
        return writer_.WriteData(std::move(work));
      });

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  //
  // TODO(37958): This is more restrictive than it needs to be, to prevent
  // reuse before on-disk free within the filesystem.
  auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));
  return barrier_.wrap(std::move(ordered_promise));
}

Journal::Promise Journal::WriteMetadata(fbl::Vector<storage::UnbufferedOperation> operations) {
  if (!journal_buffer_) {
    ZX_DEBUG_ASSERT(!writer_.IsJournalingEnabled());
    return WriteData(std::move(operations));
  }

  zx_status_t status = CheckAllWriteOperations(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Not all operations to WriteMetadata are writes\n");
    return fit::make_error_promise(status);
  }

  // Ensure there is enough space in the journal buffer.
  // Note that in addition to the operation's blocks, we also reserve space for the journal
  // entry's metadata (header, footer, etc).
  uint64_t block_count = BlockCount(operations) + kEntryMetadataBlocks;
  storage::BlockingRingBufferReservation reservation;
  status = journal_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to reserve space in journal buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the journal buffer.
  fbl::Vector<storage::BufferedOperation> buffered_operations;
  status = reservation.CopyRequests(operations, kJournalEntryHeaderBlocks, &buffered_operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to copy operations into journal buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the metadata operations to the device.
  auto promise =
      fit::make_promise([this, work = std::move(work)]() mutable -> fit::result<void, zx_status_t> {
        return writer_.WriteMetadata(std::move(work));
      });

  // Ensure all metadata operations are completed in order.
  auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  return barrier_.wrap(std::move(ordered_promise));
}

Journal::Promise Journal::TrimData(fbl::Vector<storage::BufferedOperation> operations) {
  zx_status_t status = CheckAllTrimOperations(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Not all operations to TrimData are trims\n");
    return fit::make_error_promise(status);
  }

  // Return the deferred action to write the metadata operations to the device.
  auto promise = fit::make_promise(
      [this, operations = std::move(operations)]() mutable -> fit::result<void, zx_status_t> {
        return writer_.TrimData(std::move(operations));
      });

  // Ensure all metadata operations are completed in order.
  auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  return barrier_.wrap(std::move(ordered_promise));
}

Journal::Promise Journal::Sync() {
  auto update = fit::make_promise(
      [this]() mutable -> fit::result<void, zx_status_t> { return writer_.Sync(); });
  return barrier_.sync().then(
      [update = std::move(update)](fit::context& context, fit::result<void, void>& result) mutable {
        return update(context);
      });
}

}  // namespace fs
