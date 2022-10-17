// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/journal/replay.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/vector.h>
#include <storage/operation/operation.h>

#include "entry_view.h"
#include "lib/zx/status.h"
#include "replay_tree.h"
#include "src/lib/storage/vfs/cpp/journal/format.h"
#include "src/lib/storage/vfs/cpp/journal/superblock.h"
#include "src/lib/storage/vfs/cpp/transaction/buffered_operations_builder.h"

namespace fs {
namespace {

// Reads and validates the length of the entry from a header. Ensures the payload length is not
// zero, and that the entry length does not overflow the journal buffer.
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
      cpp20::span<uint8_t>(static_cast<uint8_t*>(small_view.Data(0)), small_view.BlockSize()),
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

  // Validate the contents of the entry itself by verifying checksum (skip if built for fuzzing).
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  if (const_entry_view.footer()->checksum != const_entry_view.CalculateChecksum()) {
    return std::nullopt;
  }
#endif

  // Decode any blocks within the entry which were previously encoded (escaped).
  //
  // This way, the internal details of on-disk journal storage are hidden from the public API of
  // parsing entries.
  entry_view.DecodePayloadBlocks();

  return entry_view;
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
  // Start parsing the journal, and replay as many entries as possible.
  uint64_t entry_start = info->start();
  uint64_t sequence_number = info->sequence_number();
  FX_LOGST(INFO, "journal") << "replay: entry_start: " << entry_start
                            << ", sequence_number: " << sequence_number;
  ReplayTree operation_tree;
  while (true) {
    // Attempt to parse the next entry in the journal. Eventually, we expect this to fail.
    std::optional<const JournalEntryView> entry =
        ParseEntry(journal_buffer, entry_start, sequence_number);
    if (!entry)
      break;

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

  // Now that we've finished replaying entries, return the next sequence_number to use. It is the
  // responsibility of the caller to update the info block, but only after all prior operations have
  // been replayed.
  *out_sequence_number = sequence_number;
  *out_start = entry_start;

  for (const auto& [_, range] : operation_tree) {
    operations->push_back(range.container().operation);
  }

  return ZX_OK;
}

zx::result<JournalSuperblock> ReplayJournal(fs::TransactionHandler* transaction_handler,
                                            storage::VmoidRegistry* registry,
                                            uint64_t journal_start, uint64_t journal_length,
                                            uint32_t block_size) {
  if (journal_length <= kJournalMetadataBlocks)
    return zx::error(ZX_ERR_INVALID_ARGS);

  const uint64_t journal_entry_start = journal_start + kJournalMetadataBlocks;
  const uint64_t journal_entry_blocks = journal_length - kJournalMetadataBlocks;
  FX_LOGST(DEBUG, "journal") << "replay: Initializing journal superblock";

  // Initialize and read the journal superblock and journal buffer.
  auto journal_superblock_buffer = std::make_unique<storage::VmoBuffer>();
  zx_status_t status = journal_superblock_buffer->Initialize(registry, kJournalMetadataBlocks,
                                                             block_size, "journal-superblock");
  if (status != ZX_OK) {
    FX_LOGST(ERROR, "journal") << "Cannot initialize journal info block: "
                               << zx_status_get_string(status);
    return zx::error(status);
  }
  // Initialize and read the journal itself.
  FX_LOGST(INFO, "journal") << "replay: Initializing journal buffer (" << journal_entry_blocks
                            << " blocks)";
  storage::VmoBuffer journal_buffer;
  status = journal_buffer.Initialize(registry, journal_entry_blocks, block_size, "journal-buffer");
  if (status != ZX_OK) {
    FX_LOGST(ERROR, "journal") << "Cannot initialize journal buffer: "
                               << zx_status_get_string(status);
    return zx::error(status);
  }

  FX_LOGST(DEBUG, "journal") << "replay: Reading from storage";
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
    FX_LOGST(ERROR, "journal") << "Cannot load journal: " << zx_status_get_string(status);
    return zx::error(status);
  }

  JournalSuperblock journal_superblock(std::move(journal_superblock_buffer));
  std::vector<storage::BufferedOperation> operations;
  bool write_superblock = false;

  status = journal_superblock.Validate();
  if (status != ZX_OK) {
    // Assume that the superblock has become corrupt.  Assume that this is just the superblock that
    // is bad and that it was because a write to the info block failed.  If that has happened, then
    // it would be immediately after a flush and so no entries should need replaying.  To restore
    // the superblock, we search for the latest sequence number in the journal entries.  This code
    // mostly exists for tests that deliberately put blocks in an indeterminate state between a
    // write call and a flush, since encountering this on real devices is unlikely.
    storage::BlockBufferView view(&journal_buffer, 0, journal_entry_blocks);
    std::optional<uint64_t> sequence_number;
    for (uint64_t i = 0; i < journal_entry_blocks; ++i) {
      const auto* header = static_cast<JournalHeaderBlock*>(view.Data(i));
      if (header->prefix.magic == kJournalEntryMagic &&
          header->prefix.sequence_number > sequence_number) {
        sequence_number = header->prefix.sequence_number;
      }
    }
    if (!sequence_number) {
      // We didn't find any valid journal entries which means it's likely that the volume is
      // corrupted in an unrecoverable way, so give up.
      FX_LOGST(ERROR, "journal") << "Found corrupt superblock and no valid journal entries";
      return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
    }
    FX_LOGST(WARNING, "journal")
        << "Found corrupt superblock, but valid entries; restoring superblock";
    journal_superblock.Update(0, *sequence_number + 1);
    write_superblock = true;
  } else {
    uint64_t sequence_number = 0;
    uint64_t next_entry_start = 0;

    if (journal_superblock.start() >= journal_buffer.capacity()) {
      FX_LOGST(ERROR, "journal") << "Journal entries start beyond end of journal capacity ("
                                 << journal_superblock.start() << " vs "
                                 << journal_buffer.capacity();
      return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
    }

    // Parse the journal, deciding which entries should be replayed.
    //
    // NOTE(fxbug.dev/34510): This current implementation of replay is built against the
    // specification of the journaling format, not against how the journaling writeback code happens
    // to be implemented. In the current implementation, "write to journal" and "write to final
    // location" are tightly coupled, so although we will replay a multi-entry journal, it is
    // unlikely the disk will end up in that state. However, this use case is supported by this
    // replay code regardless.
    FX_LOGST(DEBUG, "journal") << "replay: Parsing journal entries";
    status = ParseJournalEntries(&journal_superblock, &journal_buffer, &operations,
                                 &sequence_number, &next_entry_start);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, "journal") << "Cannot parse journal entries: "
                                 << zx_status_get_string(status);
      return zx::error(status);
    }

    // Replay the requested journal entries, then the new header.
    if (!operations.empty()) {
      // Update to the new sequence_number (in-memory).
      journal_superblock.Update(next_entry_start, sequence_number);
      write_superblock = true;

      if (FX_LOG_IS_ON(INFO)) {
        for (auto& op : operations) {
          FX_LOGST(INFO, "journal")
              << "replay: writing operation @ dev_offset: " << op.op.dev_offset
              << ", vmo_offset: " << op.op.vmo_offset << ", length: " << op.op.length;
        }
      }

      status = transaction_handler->RunRequests(operations);
      if (status != ZX_OK) {
        FX_LOGST(ERROR, "journal") << "Cannot replay entries: " << zx_status_get_string(status);
        return zx::error(status);
      }

      status = transaction_handler->Flush();
      if (status != ZX_OK) {
        FX_LOGST(ERROR, "journal") << "replay: Flush failed: " << zx_status_get_string(status);
        return zx::error(status);
      }
    } else {
      FX_LOGST(DEBUG, "journal") << "replay: Not replaying entries";
    }
  }

  if (write_superblock) {
    operations.clear();
    FX_LOGST(INFO, "journal") << "replay: New start: " << journal_superblock.start()
                              << ", sequence_number: " << journal_superblock.sequence_number();
    storage::BufferedOperation operation;
    operation.vmoid = journal_superblock.buffer().vmoid();
    operation.op.type = storage::OperationType::kWrite;
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = journal_start;
    operation.op.length = kJournalMetadataBlocks;
    operations.push_back(operation);
    status = transaction_handler->RunRequests(operations);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, "journal") << "Cannot update journal superblock: "
                                 << zx_status_get_string(status);
      return zx::error(status);
    }
  }

  return zx::ok(std::move(journal_superblock));
}

}  // namespace fs
