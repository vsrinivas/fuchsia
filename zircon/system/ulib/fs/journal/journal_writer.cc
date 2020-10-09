// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <cstdio>

#include <fs/journal/internal/journal_writer.h>
#include <fs/metrics/events.h>
#include <fs/trace.h>
#include <fs/transaction/writeback.h>

#include "entry_view.h"

namespace fs {
namespace {

using storage::OperationType;

}  // namespace

namespace internal {

JournalWriter::JournalWriter(TransactionHandler* transaction_handler,
                             JournalSuperblock journal_superblock, uint64_t journal_start_block,
                             uint64_t entries_length, std::shared_ptr<JournalMetrics> metrics)
    : transaction_handler_(transaction_handler),
      journal_superblock_(std::move(journal_superblock)),
      metrics_(metrics),
      journal_start_block_(journal_start_block),
      next_sequence_number_(journal_superblock_.sequence_number()),
      next_entry_start_block_(journal_superblock_.start()),
      entries_length_(entries_length) {}

JournalWriter::JournalWriter(TransactionHandler* transaction_handler,
                             std::shared_ptr<JournalMetrics> metrics)
    : transaction_handler_(transaction_handler), metrics_(metrics) {}

fit::result<void, zx_status_t> JournalWriter::WriteData(JournalWorkItem work) {
  auto event = metrics()->NewLatencyEvent(fs_metrics::Event::kJournalWriterWriteData);
  uint32_t block_count = 0;

  // If any of the data operations we're about to write overlap with in-flight metadata
  // operations, then we risk those metadata operations "overwriting" our data blocks
  // on replay.
  //
  // Before writing data, identify that those metadata blocks should not be replayed.
  for (const auto& operation : work.operations) {
    range::Range<uint64_t> range(operation.op.dev_offset,
                                 operation.op.dev_offset + operation.op.length);
    if (live_metadata_operations_.Overlaps(range)) {
      // TODO(smklein): Write "real" revocation records instead of merely updating the info block.
      //
      // Currently, writing the info block is sufficient to "avoid metadata replay", but this
      // is only the case because the JournalWriter is synchronous, single-threaded, and
      // non-caching. If we enable asynchronous writeback, emitting revocation records
      // may be a more desirable option than "blocking until all prior operations complete,
      // then blocking on writing the info block".
      zx_status_t status = WriteInfoBlock();
      if (status != ZX_OK) {
        FS_TRACE_ERROR("journal: Failed to write data: %s\n", zx_status_get_string(status));
        return fit::error(status);
      }
      break;
    }
    block_count += operation.op.length;
  }

  event.set_block_count(block_count);
  zx_status_t status = WriteOperations(work.operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Failed to write data: %s\n", zx_status_get_string(status));
    event.set_success(false);
    return fit::error(status);
  }
  return fit::ok();
}

fit::result<void, zx_status_t> JournalWriter::WriteMetadata(JournalWorkItem work) {
  const uint64_t block_count = work.reservation.length();
  FS_TRACE_DEBUG("WriteMetadata: Writing %zu blocks (includes header, commit)\n", block_count);
  auto event = metrics()->NewLatencyEvent(fs_metrics::Event::kJournalWriterWriteMetadata);
  event.set_block_count(block_count);
  event.set_success(false);

  // Ensure the info block is caught up, so it doesn't point to the middle of an invalid entry.
  zx_status_t status = WriteInfoBlockIfIntersect(block_count);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteMetadata: Failed to write info block: %s\n", zx_status_get_string(status));
    return fit::error(status);
  }

  // Monitor the in-flight metadata operations.
  for (const auto& operation : work.operations) {
    range::Range<uint64_t> range(operation.op.dev_offset,
                                 operation.op.dev_offset + operation.op.length);
    live_metadata_operations_.Insert(std::move(range));
  }

  // Write metadata to the journal itself.
  status = WriteMetadataToJournal(&work);
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
  event.set_success(true);
  return fit::ok();
}

fit::result<void, zx_status_t> JournalWriter::TrimData(
    std::vector<storage::BufferedOperation> operations) {
  FS_TRACE_DEBUG("TrimData: trimming %zu blocks\n", BlockCount(operations));
  auto event = metrics()->NewLatencyEvent(fs_metrics::Event::kJournalWriterTrimData);

  zx_status_t status = transaction_handler_->RunRequests(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("TrimData: Failed to trim requests: %s\n", zx_status_get_string(status));
    event.set_success(false);
    return fit::error(status);
  }
  return fit::ok();
}

zx_status_t JournalWriter::WriteOperationToJournal(const storage::BlockBufferView& view) {
  const uint64_t total_block_count = view.length();
  const uint64_t max_reservation_size = EntriesLength();
  uint64_t written_block_count = 0;
  std::vector<storage::BufferedOperation> journal_operations;
  storage::BufferedOperation operation;
  operation.vmoid = view.vmoid();
  operation.op.type = storage::OperationType::kWrite;

  // Both the reservation and the on-disk location may wraparound.
  while (written_block_count != total_block_count) {
    operation.op.vmo_offset = (view.start() + written_block_count) % max_reservation_size;
    operation.op.dev_offset = EntriesStartBlock() + next_entry_start_block_;

    // The maximum number of blocks that can be written to the journal, on-disk, before needing to
    // wrap around.
    const uint64_t journal_block_max = EntriesLength() - next_entry_start_block_;
    // The maximum number of blocks that can be written from the reservation, in-memory, before
    // needing to wrap around.
    const uint64_t reservation_block_max = max_reservation_size - operation.op.vmo_offset;
    operation.op.length = std::min(total_block_count - written_block_count,
                                   std::min(journal_block_max, reservation_block_max));
    journal_operations.push_back(operation);
    written_block_count += operation.op.length;
    next_entry_start_block_ = (next_entry_start_block_ + operation.op.length) % EntriesLength();
  }

  zx_status_t status = WriteOperations(journal_operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("JournalWriter::WriteOperationToJournal: Failed to write: %s\n",
                   zx_status_get_string(status));
    return status;
  }
  return status;
}

fit::result<void, zx_status_t> JournalWriter::Sync() {
  auto event = metrics()->NewLatencyEvent(fs_metrics::Event::kJournalWriterSync);
  if (!IsWritebackEnabled()) {
    event.set_success(false);
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
    event.set_success(false);
    return fit::error(status);
  }
  return fit::ok();
}

zx_status_t JournalWriter::WriteMetadataToJournal(JournalWorkItem* work) {
  FS_TRACE_DEBUG("WriteMetadataToJournal: Writing %zu blocks with sequence_number %zu\n",
                 work->reservation.length(), next_sequence_number_);

  // Set the header and commit blocks within the journal.
  JournalEntryView entry(work->reservation.buffer_view(), work->operations,
                         next_sequence_number_++);

  zx_status_t status = WriteOperationToJournal(work->reservation.buffer_view());
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
  bool write_info =
      (head == tail) && (next_sequence_number_ != journal_superblock_.sequence_number());

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
  auto event = metrics()->NewLatencyEvent(fs_metrics::Event::kJournalWriterWriteInfoBlock);
  event.set_block_count(InfoLength());
  ZX_DEBUG_ASSERT(next_sequence_number_ > journal_superblock_.sequence_number());
  FS_TRACE_DEBUG("WriteInfoBlock: Updating sequence_number from %zu to %zu\n",
                 journal_superblock_.sequence_number(), next_sequence_number_);

  ZX_DEBUG_ASSERT(next_entry_start_block_ < EntriesLength());
  journal_superblock_.Update(next_entry_start_block_, next_sequence_number_);
  std::vector<storage::BufferedOperation> journal_operations;
  storage::BufferedOperation operation;
  operation.vmoid = journal_superblock_.buffer().vmoid();
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = InfoStartBlock();
  operation.op.length = InfoLength();
  journal_operations.push_back(operation);
  zx_status_t status = WriteOperations(journal_operations);
  if (status != ZX_OK) {
    event.set_success(false);
    return status;
  }

  // Immediately after the info block is updated, no metadata operations should be replayed
  // on reboot.
  live_metadata_operations_.Clear();
  return ZX_OK;
}

zx_status_t JournalWriter::WriteOperations(
    const std::vector<storage::BufferedOperation>& operations) {
  if (!IsWritebackEnabled()) {
    FS_TRACE_ERROR("WriteOperations: Not issuing writeback because writeback is disabled\n");
    return ZX_ERR_IO_REFUSED;
  }

  zx_status_t status = transaction_handler_->RunRequests(operations);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("WriteOperations: Failed to write requests: %s. Filesystem now read-only.\n",
                   zx_status_get_string(status));
    DisableWriteback();
    return status;
  }
  return ZX_OK;
}

}  // namespace internal
}  // namespace fs
