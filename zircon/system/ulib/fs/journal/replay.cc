// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <optional>

#include <fbl/vector.h>
#include <fs/journal/format.h>
#include <fs/journal/replay.h>
#include <fs/journal/superblock.h>
#include <fs/transaction/buffered_operations_builder.h>
#include <storage/operation/operation.h>

#include "entry_view.h"
#include "lib/zx/status.h"
#include "replay_tree.h"

namespace fs {
namespace {

// Reads and validates the length of the entry from a header.
// Ensures the payload length is not zero, and that the entry length does not overflow
// the journal buffer.
uint64_t ParseEntryLength(const storage::VmoBuffer* journal_buffer,
                          const JournalHeaderView& header) {
  uint64_t entry_length = 0;
  if (unlikely(add_overflow(header.PayloadBlocks(), kEntryMetadataBlocks, &entry_length))) {
    return 0;
  }
  if (header.PayloadBlocks() == 0 || entry_length > journal_buffer->capacity()) {
    // Zero-length entries and larger-than-buffer entries disallowed.
    return 0;
  }
  ZX_DEBUG_ASSERT(entry_length != 0);
  return entry_length;
}

std::optional<const JournalEntryView> ParseEntry(storage::VmoBuffer* journal_buffer, uint64_t start,
                                                 uint64_t sequence_number) {
  // To know how much of the journal we need to parse, first observe only one block.
  storage::BlockBufferView small_view(journal_buffer, start, 1);
  const auto header = JournalHeaderView::Create(
      fbl::Span<uint8_t>(static_cast<uint8_t*>(small_view.Data(0)), small_view.BlockSize()),
      sequence_number);

  // This is not a header block.
  if (header.is_error()) {
    return std::nullopt;
  }

  uint64_t entry_length = ParseEntryLength(journal_buffer, header.value());
  if (!entry_length) {
    return std::nullopt;
  }

  // Looks good enough. Create a JournalEntryView that now includes the footer.
  storage::BlockBufferView view(journal_buffer, start, entry_length);
  JournalEntryView entry_view(view);
  auto& const_entry_view = const_cast<const JournalEntryView&>(entry_view);

  // Validate the footer.
  if (const_entry_view.footer()->prefix.magic != kJournalEntryMagic) {
    return std::nullopt;
  }
  if (header.value().SequenceNumber() != const_entry_view.footer()->prefix.sequence_number) {
    return std::nullopt;
  }
  // Validate the contents of the entry itself.
  if (const_entry_view.footer()->checksum != const_entry_view.CalculateChecksum()) {
    return std::nullopt;
  }

  // Decode any blocks within the entry which were previously encoded (escaped).
  //
  // This way, the internal details of on-disk journal storage are hidden from the public
  // API of parsing entries.
  entry_view.DecodePayloadBlocks();

  return entry_view;
}

bool IsSubsequentEntryValid(storage::VmoBuffer* journal_buffer, uint64_t start,
                            uint64_t sequence_number) {
  // Access the current entry, but ignore everything except the "length" field.
  // WARNING: This (intentionally) does not validate the current entry.
  storage::BlockBufferView small_view(journal_buffer, start, kJournalEntryHeaderBlocks);
  const auto header = JournalHeaderView::Create(
      fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(small_view.Data(0)), small_view.BlockSize()),
      sequence_number);

  if (header.is_error()) {
    // If this isn't a header, we can't find the subsequent entry.
    return false;
  }

  // Check the next entry, if the current entry's length field is (somehow) valid.
  uint64_t entry_length = ParseEntryLength(journal_buffer, header.value());
  if (!entry_length) {
    // If we can't parse the length, then we can't check the subsequent entry.
    // If two neighboring entries are corrupted, this is treated as an interruption.
    return false;
  }
  start = (start + entry_length) % journal_buffer->capacity();
  return JournalHeaderView::Create(
             fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(journal_buffer->Data(start)),
                                journal_buffer->BlockSize()),
             sequence_number + 1)
      .is_ok();
}

void ParseBlocks(const storage::VmoBuffer& journal_buffer, const JournalEntryView& entry,
                 uint64_t entry_start, ReplayTree* operation_tree) {
  // Collect all the operations to be replayed from this entry into |operation_tree|.
  storage::BufferedOperation operation;
  for (uint32_t i = 0; i < entry.header().PayloadBlocks(); i++) {
    operation.vmoid = journal_buffer.vmoid();
    operation.op.type = storage::OperationType::kWrite;
    operation.op.vmo_offset =
        (entry_start + kJournalEntryHeaderBlocks + i) % journal_buffer.capacity();
    operation.op.dev_offset = entry.header().TargetBlock(i);
    operation.op.length = 1;

    operation_tree->insert(operation);
  }
}

}  // namespace

zx_status_t ParseJournalEntries(const JournalSuperblock* info, storage::VmoBuffer* journal_buffer,
                                std::vector<storage::BufferedOperation>* operations,
                                uint64_t* out_sequence_number, uint64_t* out_start) {
  // Validate |info| before using it.
  zx_status_t status = info->Validate();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Journal Superblock does not validate: %d\n", status);
    return status;
  }
  if (info->start() >= journal_buffer->capacity()) {
    FS_TRACE_ERROR("Journal entries start beyond end of journal capacity (%zu vs %zu)\n",
                   info->start(), journal_buffer->capacity());
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Start parsing the journal, and replay as many entries as possible.
  uint64_t entry_start = info->start();
  uint64_t sequence_number = info->sequence_number();
  FS_TRACE_INFO("replay: entry_start: %zu, sequence_number: %zu\n", entry_start, sequence_number);
  ReplayTree operation_tree;
  while (true) {
    // Attempt to parse the next entry in the journal. Eventually, we expect this to fail.
    std::optional<const JournalEntryView> entry =
        ParseEntry(journal_buffer, entry_start, sequence_number);
    if (!entry) {
      // Typically, an invalid entry will imply that the entry was interrupted
      // partway through being written. However, if the subsequent entry in the journal
      // looks valid, that implies the entry at |entry_start| was corrupted for some unknown
      // reason. The inability to replay committed journal entries may lead to filesystem
      // corruption, so we return an explicit error in this case.
      if (IsSubsequentEntryValid(journal_buffer, entry_start, sequence_number)) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      break;
    }

    if (entry->header().ObjectType() == JournalObjectType::kRevocation) {
      // TODO(fxbug.dev/34525): Revocation records advise us to avoid replaying the provided
      // operations.
      //
      // We should implement this by:
      // 1) Parsing all blocks into a non-|operations| vector
      // 2) Iterate over |operations| and look for collision
      // 3) Omit the intersect
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Replay all operations within this entry.
    ParseBlocks(*journal_buffer, *entry, entry_start, &operation_tree);

    // Move to the next entry.
    auto entry_blocks = entry->header().PayloadBlocks() + kEntryMetadataBlocks;
    entry_start = (entry_start + entry_blocks) % journal_buffer->capacity();

    // Move the sequence_number forward beyond the most recently seen entry.
    sequence_number = entry->header().SequenceNumber() + 1;
  }

  // Now that we've finished replaying entries, return the next sequence_number to use.
  // It is the responsibility of the caller to update the info block, but only after
  // all prior operations have been replayed.
  *out_sequence_number = sequence_number;
  *out_start = entry_start;

  for (const auto& [_, range] : operation_tree) {
    operations->push_back(range.container().operation);
  }

  return ZX_OK;
}

zx::status<JournalSuperblock> ReplayJournal(fs::TransactionHandler* transaction_handler,
                                            storage::VmoidRegistry* registry,
                                            uint64_t journal_start, uint64_t journal_length,
                                            uint32_t block_size) {
  const uint64_t journal_entry_start = journal_start + kJournalMetadataBlocks;
  const uint64_t journal_entry_blocks = journal_length - kJournalMetadataBlocks;
  FS_TRACE_DEBUG("replay: Initializing journal superblock\n");

  // Initialize and read the journal superblock and journal buffer.
  auto journal_superblock_buffer = std::make_unique<storage::VmoBuffer>();
  zx_status_t status = journal_superblock_buffer->Initialize(registry, kJournalMetadataBlocks,
                                                             block_size, "journal-superblock");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Cannot initialize journal info block: %d\n", status);
    return zx::error(status);
  }
  // Initialize and read the journal itself.
  FS_TRACE_INFO("replay: Initializing journal buffer (%zu blocks)\n", journal_entry_blocks);
  storage::VmoBuffer journal_buffer;
  status = journal_buffer.Initialize(registry, journal_entry_blocks, block_size, "journal-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Cannot initialize journal buffer: %d\n", status);
    return zx::error(status);
  }

  FS_TRACE_DEBUG("replay: Reading from storage\n");
  fs::BufferedOperationsBuilder builder;
  builder
      .Add(storage::Operation{.type = storage::OperationType::kRead,
                              .vmo_offset = 0,
                              .dev_offset = journal_start,
                              .length = kJournalMetadataBlocks},
           journal_superblock_buffer.get())
      .Add(storage::Operation{.type = storage::OperationType::kRead,
                              .vmo_offset = 0,
                              .dev_offset = journal_entry_start,
                              .length = journal_entry_blocks},
           &journal_buffer);
  status = transaction_handler->RunRequests(builder.TakeOperations());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Cannot load journal: %d\n", status);
    return zx::error(status);
  }

  // Parse the journal, deciding which entries should be replayed.
  //
  // NOTE(fxbug.dev/34510): This current implementation of replay is built against the specification
  // of the journaling format, not against how the journaling writeback code happens to be
  // implemented. In the current implementation, "write to journal" and "write to final location"
  // are tightly coupled, so although we will replay a multi-entry journal, it is unlikely the
  // disk will end up in that state. However, this use case is supported by this replay code
  // regardless.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  FS_TRACE_DEBUG("replay: Parsing journal entries\n");
  JournalSuperblock journal_superblock(std::move(journal_superblock_buffer));
  status = ParseJournalEntries(&journal_superblock, &journal_buffer, &operations, &sequence_number,
                               &next_entry_start);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("journal: Cannot parse journal entries: %d\n", status);
    return zx::error(status);
  }

  // Replay the requested journal entries, then the new header.
  if (!operations.empty()) {
    // Update to the new sequence_number (in-memory).
    journal_superblock.Update(next_entry_start, sequence_number);

    for (auto& op : operations) {
      FS_TRACE_INFO("replay: writing operation @ dev_offset: %zu, vmo_offset: %zu, length: %zu\n",
                    op.op.dev_offset, op.op.vmo_offset, op.op.length);
    }

    status = transaction_handler->RunRequests(operations);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("journal: Cannot replay entries: %d\n", status);
      return zx::error(status);
    }

    operations.clear();
    FS_TRACE_INFO("replay: New start: %zu, sequence_number: %zu\n", next_entry_start,
                  sequence_number);
    storage::BufferedOperation operation;
    operation.vmoid = journal_superblock.buffer().vmoid();
    operation.op.type = storage::OperationType::kWrite;
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = journal_start;
    operation.op.length = kJournalMetadataBlocks;
    operations.push_back(operation);
    status = transaction_handler->RunRequests(operations);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("journal: Cannot update journal superblock: %d\n", status);
      return zx::error(status);
    }

  } else {
    FS_TRACE_DEBUG("replay: Not replaying entries\n");
  }

  return zx::ok(std::move(journal_superblock));
}

}  // namespace fs
