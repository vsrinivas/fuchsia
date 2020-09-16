// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <zircon/status.h>

#include <fs/journal/journal.h>
#include <fs/trace.h>
#include <fs/transaction/writeback.h>
#include <safemath/checked_math.h>

#include "entry_view.h"
#include "format_assertions.h"

namespace fs {
namespace {

template <storage::OperationType type, typename T>
zx::status<uint64_t> CheckOperationsAndGetTotalBlockCount(const T& operations) {
  uint64_t total_blocks = 0;
  for (const auto& operation : operations) {
    if (operation.op.type != type) {
      FS_TRACE_ERROR("journal: Unexpected operation type (actual=%u, expected=%u)\n",
                     operation.op.type, type);
      return zx::error(ZX_ERR_WRONG_TYPE);
    }
    if (!safemath::CheckAdd(total_blocks, operation.op.length).AssignIfValid(&total_blocks)) {
      FS_TRACE_ERROR("journal: Too many blocks\n");
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
  }
  // Make sure there's enough for kEntryMetadataBlocks without overflowing, but don't include that
  // in the result.
  if (!safemath::CheckAdd(total_blocks, kEntryMetadataBlocks).IsValid()) {
    FS_TRACE_ERROR("journal: Too many blocks\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(total_blocks);
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
                 uint64_t journal_start_block, Options options)
    : journal_buffer_(std::move(journal_buffer)),
      writeback_buffer_(std::move(writeback_buffer)),
      writer_(transaction_handler, std::move(journal_superblock), journal_start_block,
              journal_buffer_->capacity()),
      options_(options) {
  // For now, the ring buffers must use the same block size as kJournalBlockSize.
  ZX_ASSERT(journal_buffer_->BlockSize() == kJournalBlockSize);
  ZX_ASSERT(writeback_buffer_->BlockSize() == kJournalBlockSize);
}

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

Journal::Promise Journal::WriteData(std::vector<storage::UnbufferedOperation> operations) {
  auto block_count_or =
      CheckOperationsAndGetTotalBlockCount<storage::OperationType::kWrite>(operations);
  if (block_count_or.is_error()) {
    return fit::make_error_promise(block_count_or.status_value());
  }
  if (block_count_or.value() == 0) {
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  }

  storage::BlockingRingBufferReservation reservation;
  zx_status_t status = writeback_buffer_->Reserve(block_count_or.value(), &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to reserve space in writeback buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the buffer.
  std::vector<storage::BufferedOperation> buffered_operations;
  auto result = reservation.CopyRequests(operations, 0, &buffered_operations);
  if (result.is_error()) {
    FS_TRACE_ERROR("journal: Failed to copy operations into writeback buffer: %s\n",
                   result.status_string());
    return fit::make_error_promise(result.error_value());
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the data operations to the device.
  auto promise =
      fit::make_promise([this, work = std::move(work)]() mutable -> fit::result<void, zx_status_t> {
        return writer_.WriteData(std::move(work));
      });

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  if (options_.sequence_data_writes) {
    auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));
    return barrier_.wrap(std::move(ordered_promise));
  } else {
    return barrier_.wrap(std::move(promise));
  }
}

Journal::Promise Journal::WriteMetadata(std::vector<storage::UnbufferedOperation> operations) {
  if (!journal_buffer_) {
    ZX_DEBUG_ASSERT(!writer_.IsJournalingEnabled());
    return WriteData(std::move(operations));
  }

  auto block_count_or =
      CheckOperationsAndGetTotalBlockCount<storage::OperationType::kWrite>(operations);
  if (block_count_or.is_error()) {
    return fit::make_error_promise(block_count_or.status_value());
  }

  // Ensure there is enough space in the journal buffer.
  // Note that in addition to the operation's blocks, we also reserve space for the journal
  // entry's metadata (header, footer, etc).
  uint64_t block_count = block_count_or.value() + kEntryMetadataBlocks;
  storage::BlockingRingBufferReservation reservation;
  zx_status_t status = journal_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to reserve space in journal buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the journal buffer.
  std::vector<storage::BufferedOperation> buffered_operations;
  auto result =
      reservation.CopyRequests(operations, kJournalEntryHeaderBlocks, &buffered_operations);
  if (result.is_error()) {
    FS_TRACE_ERROR("journal: Failed to copy operations into journal buffer: %s\n",
                   result.status_string());
    return fit::make_error_promise(result.error_value());
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the metadata operations to the device.
  auto promise =
      fit::make_promise([this, work = std::move(work)]() mutable -> fit::result<void, zx_status_t> {
        fit::result<void, zx_status_t> result = writer_.WriteMetadata(std::move(work));
        if (write_metadata_callback_) {
          write_metadata_callback_(result.is_ok() ? ZX_OK : result.error());
        }
        return result;
      });

  // Ensure all metadata operations are completed in order.
  auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  return barrier_.wrap(std::move(ordered_promise));
}

Journal::Promise Journal::TrimData(std::vector<storage::BufferedOperation> operations) {
  zx_status_t status =
      CheckOperationsAndGetTotalBlockCount<storage::OperationType::kTrim>(operations)
          .status_value();
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
