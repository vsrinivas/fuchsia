// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/journal/journal.h"

#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>
#include <zircon/status.h>

#include <safemath/checked_math.h>

#include "entry_view.h"
#include "format_assertions.h"
#include "storage/operation/unbuffered_operation.h"

namespace fs {
namespace {

template <storage::OperationType type, typename T>
zx::result<uint64_t> CheckOperationsAndGetTotalBlockCount(const T& operations) {
  uint64_t total_blocks = 0;
  for (const auto& operation : operations) {
    if (operation.op.type != type) {
      FX_LOGST(ERROR, "journal") << "Unexpected operation type (actual="
                                 << static_cast<int>(operation.op.type)
                                 << ", expected=" << static_cast<int>(type) << ")";
      return zx::error(ZX_ERR_WRONG_TYPE);
    }
    if (!safemath::CheckAdd(total_blocks, operation.op.length).AssignIfValid(&total_blocks)) {
      FX_LOGST(ERROR, "journal") << "Too many blocks";
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
  }
  // Make sure there's enough for kEntryMetadataBlocks without overflowing, but don't include that
  // in the result.
  if (!safemath::CheckAdd(total_blocks, kEntryMetadataBlocks).IsValid()) {
    FX_LOGST(ERROR, "journal") << "Too many blocks";
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(total_blocks);
}

fpromise::result<void, zx_status_t> SignalSyncComplete(sync_completion_t* completion) {
  FX_LOGST(DEBUG, "journal") << "SignalSyncComplete";
  sync_completion_signal(completion);
  return fpromise::ok();
}

fpromise::result<> ToVoidError(fpromise::result<void, zx_status_t> result) {
  if (result.is_ok()) {
    return fpromise::ok();
  } else {
    return fpromise::error();
  }
}

}  // namespace

Journal::Journal(TransactionHandler* transaction_handler, JournalSuperblock journal_superblock,
                 std::unique_ptr<storage::BlockingRingBuffer> journal_buffer,
                 std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer,
                 uint64_t journal_start_block)
    : journal_buffer_(std::move(journal_buffer)),
      writeback_buffer_(std::move(writeback_buffer)),
      writer_(transaction_handler, std::move(journal_superblock), journal_start_block,
              journal_buffer_->capacity()) {
  // For now, the ring buffers must use the same block size as kJournalBlockSize.
  ZX_ASSERT(journal_buffer_->BlockSize() == kJournalBlockSize);
  ZX_ASSERT(writeback_buffer_->BlockSize() == kJournalBlockSize);
  FX_LOGST(DEBUG, "journal") << "Created Journal, start block: " << journal_start_block
                             << ", capacity: " << journal_buffer_->capacity();
}

Journal::~Journal() {
  sync_completion_t completion;
  schedule_task(Sync().then([&completion](const fpromise::result<void, zx_status_t>& result) {
    return SignalSyncComplete(&completion);
  }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  executor_.Terminate();
}

void Journal::FlushPending() {
  if (pending_ == 0)
    return;

  // Writes to the journal can only proceed once all data writes have been flushed.
  if (journal_data_barrier_) {
    schedule_task(data_barrier_.sync()
                      .and_then([this]() { return ToVoidError(writer_.Flush()); })
                      .and_then(std::move(journal_data_barrier_)));
  }

  // Once all the journal writes are done, we need to flush again to flush the writes to their final
  // locations.
  schedule_task(journal_sequencer_.wrap(fpromise::make_promise([this]() -> fpromise::result<> {
    if (writer_.HavePendingWork()) {
      return ToVoidError(writer_.Flush());
    } else {
      return fpromise::ok();
    }
  })));

  // Blocks will still be reserved, but they'll shortly be in-flight and later released.
  pending_ = 0;
}

Journal::Promise Journal::WriteData(std::vector<storage::UnbufferedOperation> operations) {
  auto block_count_or =
      CheckOperationsAndGetTotalBlockCount<storage::OperationType::kWrite>(operations);
  if (block_count_or.is_error()) {
    return fpromise::make_error_promise(block_count_or.status_value());
  }
  if (block_count_or.value() == 0) {
    return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
  }

  storage::BlockingRingBufferReservation reservation;
  zx_status_t status = writeback_buffer_->Reserve(block_count_or.value(), &reservation);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, "journal") << "Failed to reserve space in writeback buffer: "
                               << zx_status_get_string(status);
    return fpromise::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the buffer.
  std::vector<storage::BufferedOperation> buffered_operations;
  auto result = reservation.CopyRequests(operations, 0, &buffered_operations);
  if (result.is_error()) {
    FX_LOGST(ERROR, "journal") << "Failed to copy operations into writeback buffer: "
                               << result.status_string();
    return fpromise::make_error_promise(result.error_value());
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the data operations to the device.
  return fpromise::make_promise(
      [this, work = std::move(work)]() mutable { return writer_.WriteData(std::move(work)); });
}

zx_status_t Journal::CommitTransaction(Transaction transaction) {
  if (transaction.metadata_operations.empty()) {
    // For now, data must always be written with metadata and trim must come with metadata.
    if (transaction.data_promise) {
      FX_LOGST(ERROR, "journal") << "data_promise specified, but no metadata operations added";
      return ZX_ERR_INVALID_ARGS;
    }
    if (!transaction.trim.empty()) {
      FX_LOGST(ERROR, "journal") << "trim ops added without at least one metadata operation";
      return ZX_ERR_INVALID_ARGS;
    }
    if (transaction.commit_callback)
      transaction.commit_callback();
    if (transaction.complete_callback)
      transaction.complete_callback();
    return ZX_OK;
  }

  if (!writer_.IsWritebackEnabled()) {
    FX_LOGST(DEBUG, "journal") << "Not commiting; writeback disabled";
    return ZX_ERR_IO_REFUSED;
  }

  auto block_count_or = CheckOperationsAndGetTotalBlockCount<storage::OperationType::kWrite>(
      transaction.metadata_operations);
  if (block_count_or.is_error()) {
    return block_count_or.status_value();
  }

  if (block_count_or.value() > kMaxBlockDescriptors) {
    FX_LOGST(ERROR, "journal") << "block_count (" << block_count_or.value() << ") exceeds maximum "
                               << kMaxBlockDescriptors;
    return ZX_ERR_NO_SPACE;
  }

  // Ensure there is enough space in the journal buffer. Note that in addition to the operation's
  // blocks, we also reserve space for the journal entry's metadata (header, footer, etc).
  uint64_t block_count = block_count_or.value() + kEntryMetadataBlocks;
  storage::BlockingRingBufferReservation reservation;
  if (pending_ + block_count > journal_buffer_->capacity()) {
    // Unblock writes to the journal.
    FlushPending();
  }
  zx_status_t status = journal_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, "journal") << "Failed to reserve space in journal buffer: "
                               << zx_status_get_string(status);
    return status;
  }

  // Once we have that space, copy the operations into the journal buffer.
  std::vector<storage::BufferedOperation> buffered_operations;
  auto result = reservation.CopyRequests(transaction.metadata_operations, kJournalEntryHeaderBlocks,
                                         &buffered_operations);
  if (result.is_error()) {
    FX_LOGST(ERROR, "journal") << "Failed to copy operations into journal buffer: "
                               << result.status_string();
    return result.error_value();
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));
  work.commit_callback = std::move(transaction.commit_callback);
  if (write_metadata_callback_) {
    work.complete_callback = [this, callback = std::move(transaction.complete_callback)]() mutable {
      if (callback)
        callback();
      write_metadata_callback_();
    };
  } else {
    work.complete_callback = std::move(transaction.complete_callback);
  }

  std::optional<internal::JournalWorkItem> trim_work;
  if (!transaction.trim.empty()) {
    trim_work = internal::JournalWorkItem({}, std::move(transaction.trim));
  }

  auto promise = fpromise::make_promise(
      [this, work = std::move(work),
       trim_work = std::move(trim_work)]() mutable -> fpromise::result<void, zx_status_t> {
        fpromise::result<void, zx_status_t> result =
            writer_.WriteMetadata(std::move(work), std::move(trim_work));
        return result;
      });

  // journal_sequencer_ is used to keep all metadata operations in order.
  if (!journal_data_barrier_ && transaction.data_promise) {
    // If this transaction has data, we need to block writes to the journal until the data has been
    // flushed, so to do that, we add a blocking promise that we'll post later after the data has
    // been flushed.
    journal_data_barrier_ =
        journal_sequencer_.wrap(fpromise::make_ok_promise()).take_continuation();
  }
  pending_ += block_count;
  auto ordered_promise = journal_sequencer_.wrap(std::move(promise));

  fpromise::pending_task task;
  if (transaction.data_promise) {
    task = data_barrier_.wrap(std::move(transaction.data_promise))
               .and_then(std::move(ordered_promise));
  } else {
    task = std::move(ordered_promise);
  }

  schedule_task(std::move(task));
  return ZX_OK;
}

Journal::Promise Journal::Sync() {
  FlushPending();
  return journal_sequencer_.wrap(fpromise::make_promise([this] { return writer_.Sync(); }));
}

}  // namespace fs
