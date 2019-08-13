// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <blobfs/journal/journal2.h>
#include <fs/trace.h>

#include "entry-view.h"

namespace blobfs {
namespace {

zx_status_t CheckAllWriteOperations(const fbl::Vector<UnbufferedOperation>& operations) {
  for (const auto& operation : operations) {
    if (operation.op.type != OperationType::kWrite) {
      FS_TRACE_ERROR("blobfs: Transmitted non-write operation to writeback\n");
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

namespace internal {

JournalWriter::JournalWriter(fs::TransactionHandler* transaction_handler,
                             JournalSuperblock journal_superblock, uint64_t journal_start_block,
                             uint64_t entries_length)
    : transaction_handler_(transaction_handler),
      journal_superblock_(std::move(journal_superblock)),
      journal_start_block_(journal_start_block),
      next_sequence_number_(journal_superblock_.sequence_number()),
      next_entry_start_block_(journal_superblock_.start()),
      entries_length_(entries_length) {}

JournalWriter::JournalWriter(fs::TransactionHandler* transaction_handler)
    : transaction_handler_(transaction_handler) {}

fit::result<void, zx_status_t> JournalWriter::WriteData(JournalWorkItem work) {
  zx_status_t status = WriteOperations(work.operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to write data: %s\n", zx_status_get_string(status));
    return fit::error(status);
  }
  return fit::ok();
}

fit::result<void, zx_status_t> JournalWriter::WriteMetadata(JournalWorkItem work,
                                                            uint64_t block_count) {
  FS_TRACE_DEBUG("WriteMetadata: Writing %zu blocks (includes header, commit)\n", block_count);

  // Ensure the info block is caught up, so it doesn't point to the middle of an invalid entry.
  zx_status_t status = WriteInfoBlockIfIntersect(block_count);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteMetadata: Failed to write info block: %s\n", zx_status_get_string(status));
    return fit::error(status);
  }

  // Write metadata to the journal itself.
  status = WriteMetadataToJournal(&work, block_count);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteMetadata: Failed to write metadata to journal: %s\n",
                   zx_status_get_string(status));
    return fit::error(status);
  }

  // Write metadata to the final on-disk, non-journal location.
  status = WriteOperations(work.operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteMetadata: Failed to write metadata to final location: %s\n",
                   zx_status_get_string(status));
    return fit::error(status);
  }
  return fit::ok();
}

fit::result<void, zx_status_t> JournalWriter::Sync() {
  if (!IsWritebackEnabled()) {
    return fit::error(ZX_ERR_IO_REFUSED);
  }
  if (!IsJournalingEnabled()) {
    return fit::ok();
  }

  if (next_sequence_number_ == journal_superblock_.sequence_number()) {
    FS_TRACE_DEBUG("Sync: Skipping write to info block (no sequence update)\n");
    return fit::ok();
  }

  zx_status_t status = WriteInfoBlock();
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok();
}

zx_status_t JournalWriter::WriteMetadataToJournal(JournalWorkItem* work, uint64_t block_count) {
  FS_TRACE_DEBUG("WriteMetadataToJournal: Writing %zu blocks with sequence_number %zu\n",
                 block_count, next_sequence_number_);

  // Set the header and commit blocks within the journal.
  JournalEntryView entry(work->reservation.buffer_view(), work->operations,
                         next_sequence_number_++);

  // Create new operation(s) which target the on-disk journal, utilizing the buffer
  // that was prepared by the |JournalEntryView|.
  fbl::Vector<BufferedOperation> journal_operations;
  BufferedOperation operation;
  operation.vmoid = work->reservation.vmoid();
  operation.op.type = OperationType::kWrite;
  operation.op.vmo_offset = work->reservation.start();
  operation.op.dev_offset = EntriesStartBlock() + next_entry_start_block_;
  size_t block_count_max = EntriesLength() - next_entry_start_block_;
  operation.op.length = std::min(block_count, block_count_max);
  journal_operations.push_back(operation);
  FS_TRACE_DEBUG("WriteMetadataToJournal: Write %zu blocks. VMO: %zu to dev: %zu\n",
                 operation.op.length, operation.op.vmo_offset, operation.op.dev_offset);

  // Journal wraparound case.
  if (block_count > block_count_max) {
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = EntriesStartBlock();
    operation.op.length = block_count - block_count_max;
    journal_operations.push_back(operation);

    FS_TRACE_DEBUG("WriteMetadataToJournal: (wrap) Write %zu blocks. VMO: %zu to dev: %zu\n",
                   operation.op.length, operation.op.vmo_offset, operation.op.dev_offset);
  }
  next_entry_start_block_ = (next_entry_start_block_ + block_count) % EntriesLength();
  zx_status_t status = WriteOperations(journal_operations);

  // Although the payload may be encoded while written to the journal, it should be decoded
  // when written to the final on-disk location later.
  entry.DecodePayloadBlocks();
  return status;
}

zx_status_t JournalWriter::WriteInfoBlockIfIntersect(uint64_t block_count) {
  // We need to write the info block now if [journal tail, journal tail + block_count)
  // intersects with [journal head, journal tail).
  //
  // Logically, the journal is a circular buffer:
  //
  //   [ ____, ____, ____, ____, ____, ____ ]
  //
  // Within that buffer, the journal has some entries which will be replayed
  //
  //           Info Block        Next Entry Start Block
  //           |                 |
  //   [ ____, head, data, tail, ____, ____ ]
  //
  // In this diagram, it would be safe to write one, two, or three additional blocks:
  // they would fit within the journal. However, if four blocks are written, the journal
  // would "eat its own head":
  //
  //           Info Block
  //           |
  //   [ blk3, blk4, data, tail, blk1, blk2 ]
  //           |
  //           Collision!
  //
  // If power failure occurred, replay would be unable to parse prior entries, since the
  // start block would point to an invalid entry. However, if we also wrote the info block
  // repeatedly, the journaling code would incur a significant write amplification cost.
  //
  // To compromise, we write the info block before any writes that would trigger this collision.
  const uint64_t head = journal_superblock_.start();
  const uint64_t tail = next_entry_start_block_;
  const uint64_t capacity = EntriesLength();

  // It's a little tricky to distinguish between an "empty" and "full" journal, so we observe
  // that case explicitly first, using the sequence number to make the distinction.
  //
  // We require an info block update if the journal is full, but not if it's empty.
  bool write_info = (head == tail) &&
                    (next_sequence_number_ != journal_superblock_.sequence_number());

  if (!write_info) {
    const uint64_t journal_used = (head <= tail) ? (tail - head) : ((capacity - head) + tail);
    const uint64_t journal_free = capacity - journal_used;
    if (journal_free < block_count) {
      FS_TRACE_DEBUG("WriteInfoBlockIfIntersect: Writing info block (can't write %zu blocks)\n",
                    block_count);
      write_info = true;
    } else {
      FS_TRACE_DEBUG("WriteInfoBlockIfIntersect: Not writing info (have %zu, need %zu blocks)\n",
                     journal_free, block_count);
    }
  }

  if (write_info) {
    zx_status_t status = WriteInfoBlock();
    if (status != ZX_OK) {
      FS_TRACE_ERROR("WriteInfoBlockIfIntersect: Failed to write info block\n");
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t JournalWriter::WriteInfoBlock() {
  ZX_DEBUG_ASSERT(next_sequence_number_ > journal_superblock_.sequence_number());
  FS_TRACE_DEBUG("WriteInfoBlock: Updating sequence_number from %zu to %zu\n",
                 journal_superblock_.sequence_number(), next_sequence_number_);

  ZX_DEBUG_ASSERT(next_entry_start_block_ < EntriesLength());
  journal_superblock_.Update(next_entry_start_block_, next_sequence_number_);
  fbl::Vector<BufferedOperation> journal_operations;
  BufferedOperation operation;
  operation.vmoid = journal_superblock_.buffer().vmoid();
  operation.op.type = OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = InfoStartBlock();
  operation.op.length = InfoLength();
  journal_operations.push_back(operation);
  return WriteOperations(journal_operations);
}

zx_status_t JournalWriter::WriteOperations(const fbl::Vector<BufferedOperation>& operations) {
  if (!IsWritebackEnabled()) {
    FS_TRACE_ERROR("WriteOperations: Not issuing writeback because writeback is disabled\n");
    return ZX_ERR_IO_REFUSED;
  }

  zx_status_t status = FlushWriteRequests(transaction_handler_, operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteOperations: Failed to write requests: %s. Filesystem now read-only.\n",
                   zx_status_get_string(status));
    DisableWriteback();
    return status;
  }
  return ZX_OK;
}

}  // namespace internal

Journal2::Journal2(fs::TransactionHandler* transaction_handler,
                   JournalSuperblock journal_superblock,
                   std::unique_ptr<BlockingRingBuffer> journal_buffer,
                   std::unique_ptr<BlockingRingBuffer> writeback_buffer,
                   uint64_t journal_start_block)
    : journal_buffer_(std::move(journal_buffer)),
      writeback_buffer_(std::move(writeback_buffer)),
      writer_(transaction_handler, std::move(journal_superblock), journal_start_block,
              journal_buffer_->capacity()) {}

Journal2::Journal2(fs::TransactionHandler* transaction_handler,
                   std::unique_ptr<BlockingRingBuffer> writeback_buffer)
    : writeback_buffer_(std::move(writeback_buffer)), writer_(transaction_handler) {}

Journal2::~Journal2() {
  sync_completion_t completion;
  schedule_task(Sync().then([&completion](const fit::result<void, zx_status_t>& result) {
    return SignalSyncComplete(&completion);
  }));
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

Journal2::Promise Journal2::WriteData(fbl::Vector<UnbufferedOperation> operations) {
  zx_status_t status = CheckAllWriteOperations(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Not all operations to WriteData are writes\n");
    return fit::make_error_promise(status);
  }

  // Ensure there is enough space in the writeback buffer.
  uint64_t block_count = BlockCount(operations);
  BlockingRingBufferReservation reservation;
  status = writeback_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to reserve space in writeback buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the buffer.
  fbl::Vector<BufferedOperation> buffered_operations;
  status = reservation.CopyRequests(operations, 0, &buffered_operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to copy operations into writeback buffer: %s\n",
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
  return barrier_.wrap(std::move(promise));
}

Journal2::Promise Journal2::WriteMetadata(fbl::Vector<UnbufferedOperation> operations) {
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
  BlockingRingBufferReservation reservation;
  status = journal_buffer_->Reserve(block_count, &reservation);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to reserve space in journal buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }

  // Once we have that space, copy the operations into the journal buffer.
  fbl::Vector<BufferedOperation> buffered_operations;
  status = reservation.CopyRequests(operations, kJournalEntryHeaderBlocks, &buffered_operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to copy operations into journal buffer: %s\n",
                   zx_status_get_string(status));
    return fit::make_error_promise(status);
  }
  internal::JournalWorkItem work(std::move(reservation), std::move(buffered_operations));

  // Return the deferred action to write the metadata operations to the device.
  auto promise = fit::make_promise(
      [this, work = std::move(work), block_count]() mutable -> fit::result<void, zx_status_t> {
        return writer_.WriteMetadata(std::move(work), block_count);
      });

  // Ensure all metadata operations are completed in order.
  auto ordered_promise = metadata_sequencer_.wrap(std::move(promise));

  // Track write ops to ensure that invocations of |sync| can flush all prior work.
  return barrier_.wrap(std::move(ordered_promise));
}

Journal2::Promise Journal2::WriteRevocation(fbl::Vector<Operation> operations) {
  if (!writer_.IsJournalingEnabled()) {
    return fit::make_promise([]() -> fit::result<void, zx_status_t> { return fit::ok(); });
  }

  // TODO(ZX-4752): Don't forget to wrap your promises (metadata order + sync).
  // See: "metadata_sequencer_", "barrier_".
  return fit::make_error_promise(ZX_ERR_NOT_SUPPORTED);
}

Journal2::Promise Journal2::Sync() {
  auto update = fit::make_promise(
      [this]() mutable -> fit::result<void, zx_status_t> { return writer_.Sync(); });
  return barrier_.sync().then(
      [update = std::move(update)](fit::context& context, fit::result<void, void>& result) mutable {
        return update(context);
      });
}

}  // namespace blobfs
