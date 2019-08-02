// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <optional>

#include <blobfs/format.h>
#include <blobfs/journal/replay.h>
#include <blobfs/journal/superblock.h>
#include <blobfs/operation.h>
#include <blobfs/writeback.h>
#include <fbl/vector.h>

#include "entry-view.h"

namespace blobfs {
namespace {

// Reads and validates the length of the entry from a header.
// Ensures the payload length is not zero, and that the entry length does not overflow
// the journal buffer.
uint64_t ParseEntryLength(const VmoBuffer* journal_buffer, const JournalHeaderBlock* header) {
  uint64_t entry_length = 0;
  if (unlikely(add_overflow(header->payload_blocks, kEntryMetadataBlocks, &entry_length))) {
    return 0;
  }
  if (header->payload_blocks == 0 || entry_length > journal_buffer->capacity()) {
    // Zero-length entries and larger-than-buffer entries disallowed.
    return 0;
  }
  ZX_DEBUG_ASSERT(entry_length != 0);
  return entry_length;
}

bool IsJournalMetadata(const JournalHeaderBlock* header, uint64_t sequence_number) {
  if (header->prefix.magic != kJournalEntryMagic) {
    return false;
  }
  if (header->prefix.sequence_number != sequence_number) {
    return false;
  }
  return true;
}

bool IsHeader(const JournalHeaderBlock* header, uint64_t sequence_number) {
  if (!IsJournalMetadata(header, sequence_number)) {
    return false;
  }
  if (header->prefix.ObjectType() != JournalObjectType::kHeader) {
    return false;
  }
  return true;
}

std::optional<const JournalEntryView> ParseEntry(VmoBuffer* journal_buffer, uint64_t start,
                                                 uint64_t sequence_number) {
  // To know how much of the journal we need to parse, first observe only one block.
  BlockBufferView small_view(journal_buffer, start, 1);
  const JournalEntryView header_entry_view(std::move(small_view));

  // Before trying to access the commit block, check the header.
  if (!IsHeader(header_entry_view.header(), sequence_number)) {
    return std::nullopt;
  }
  uint64_t entry_length = ParseEntryLength(journal_buffer, header_entry_view.header());
  if (!entry_length) {
    return std::nullopt;
  }

  // Looks good enough. Create a JournalEntryView that now includes the footer.
  BlockBufferView view(journal_buffer, start, entry_length);
  JournalEntryView entry_view(std::move(view));
  auto& const_entry_view = const_cast<const JournalEntryView&>(entry_view);

  // Validate the footer.
  if (const_entry_view.footer()->prefix.magic != kJournalEntryMagic) {
    return std::nullopt;
  }
  if (const_entry_view.header()->prefix.sequence_number !=
      const_entry_view.footer()->prefix.sequence_number) {
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

bool IsSubsequentEntryValid(VmoBuffer* journal_buffer, uint64_t start, uint64_t sequence_number) {
  // Access the current entry, but ignore everything except the "length" field.
  // WARNING: This (intentionally) does not validate the current entry.
  BlockBufferView small_view(journal_buffer, start, kJournalEntryHeaderBlocks);
  const JournalEntryView header_entry_view(std::move(small_view));
  if (!IsHeader(header_entry_view.header(), sequence_number)) {
    // If this isn't a header, we can't find the subsequent entry.
    return false;
  }

  // Check the next entry, if the current entry's length field is (somehow) valid.
  uint64_t entry_length = ParseEntryLength(journal_buffer, header_entry_view.header());
  if (!entry_length) {
    // If we can't parse the length, then we can't check the subsequent entry.
    // If two neighboring entries are corrupted, this is treated as an interruption.
    return false;
  }
  start = (start + entry_length) % journal_buffer->capacity();
  const auto entry_view = JournalEntryView(BlockBufferView(journal_buffer, start, 1));
  return IsJournalMetadata(entry_view.header(), sequence_number + 1);
}

void ParseBlocks(const VmoBuffer& journal_buffer, const JournalEntryView& entry,
                 uint64_t entry_start, fbl::Vector<BufferedOperation>* operations) {
  // Collect all the operations to be replayed from this entry into |operations|.
  BufferedOperation operation;
  for (size_t i = 0; i < entry.header()->payload_blocks; i++) {
    operation.vmoid = journal_buffer.vmoid();
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset =
        (entry_start + kJournalEntryHeaderBlocks + i) % journal_buffer.capacity();
    operation.op.dev_offset = entry.header()->target_blocks[i];
    operation.op.length = 1;

    // Small optimization for sequential operations. If elements are contiguous for N
    // blocks, write back one operation, rather than N operations.
    if (operations->size() > 0) {
      auto& last = (*operations)[operations->size() - 1].op;
      if ((last.vmo_offset + last.length == operation.op.vmo_offset) &&
          (last.dev_offset + last.length == operation.op.dev_offset)) {
        last.length++;
        continue;
      }
    }
    operations->push_back(std::move(operation));
  }
}

}  // namespace

zx_status_t ParseJournalEntries(const JournalSuperblock* info, VmoBuffer* journal_buffer,
                                fbl::Vector<BufferedOperation>* operations,
                                uint64_t* out_sequence_number) {
  // Validate |info| before using it.
  zx_status_t status = info->Validate();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Journal Superblock does not validate: %d\n", status);
    return status;
  }

  // Start parsing the journal, and replay as many entries as possible.
  uint64_t entry_start = info->start();
  uint64_t sequence_number = info->sequence_number();
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

    if (entry->header()->prefix.ObjectType() == JournalObjectType::kRevocation) {
      // TODO(ZX-4752): Revocation records advise us to avoid replaying the provided
      // operations.
      //
      // We should implement this by:
      // 1) Parsing all blocks into a non-|operations| vector
      // 2) Iterate over |operations| and look for collision
      // 3) Omit the intersect
      return ZX_ERR_NOT_SUPPORTED;
    } else {
      // Replay all operations within this entry.
      ParseBlocks(*journal_buffer, *entry, entry_start, operations);
    }

    // Move to the next entry.
    auto entry_blocks = entry->header()->payload_blocks + kEntryMetadataBlocks;
    entry_start += entry_blocks % journal_buffer->capacity();

    // Move the sequence_number forward beyond the most recently seen entry.
    sequence_number = entry->header()->prefix.sequence_number + 1;
  }

  // Now that we've finished replaying entries, return the next sequence_number to use.
  // It is the responsibility of the caller to update the info block, but only after
  // all prior operations have been replayed.
  *out_sequence_number = sequence_number;
  return ZX_OK;
}

zx_status_t ReplayJournal(fs::TransactionHandler* transaction_handler, VmoidRegistry* registry,
                          uint64_t journal_start, uint64_t journal_length,
                          JournalSuperblock* out_journal_superblock) {
  const uint64_t journal_entry_start = journal_start + kJournalMetadataBlocks;
  const uint64_t journal_entry_blocks = journal_length - kJournalMetadataBlocks;
  FS_TRACE_DEBUG("replay: Initializing journal superblock\n");

  // Initialize and read the journal superblock and journal buffer.
  auto journal_superblock_buffer = std::make_unique<VmoBuffer>();
  zx_status_t status =
      journal_superblock_buffer->Initialize(registry, kJournalMetadataBlocks, "journal-superblock");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot initialize journal info block: %d\n", status);
    return status;
  }
  // Initialize and read the journal itself.
  FS_TRACE_DEBUG("replay: Initializing journal buffer\n");
  VmoBuffer journal_buffer;
  status = journal_buffer.Initialize(registry, journal_entry_blocks, "journal-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot initialize journal buffer: %d\n", status);
    return status;
  }

  FS_TRACE_DEBUG("replay: Reading from storage\n");
  fs::ReadTxn transaction(transaction_handler);
  transaction.Enqueue(journal_superblock_buffer->vmoid(), 0, journal_start, kJournalMetadataBlocks);
  transaction.Enqueue(journal_buffer.vmoid(), 0, journal_entry_start, journal_entry_blocks);
  status = transaction.Transact();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot load journal: %d\n", status);
    return status;
  }

  // Parse the journal, deciding which entries should be replayed.
  //
  // NOTE(ZX-4737): This current implementation of replay is built against the specification of
  // the journaling format, not against how the journaling writeback code happens to be
  // implemented. In the current implementation, "write to journal" and "write to final location"
  // are tightly coupled, so although we will replay a multi-entry journal, it is unlikely the
  // disk will end up in that state. However, this use case is supported by this replay code
  // regardless.
  fbl::Vector<BufferedOperation> operations;
  uint64_t sequence_number = 0;
  FS_TRACE_DEBUG("replay: Parsing journal entries\n");
  JournalSuperblock journal_superblock(std::move(journal_superblock_buffer));
  status = ParseJournalEntries(&journal_superblock, &journal_buffer, &operations, &sequence_number);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot parse journal entries: %d\n", status);
    return status;
  }

  // Replay the requested journal entries.
  if (operations.size() > 0) {
    status = FlushWriteRequests(transaction_handler, operations);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs: Cannot replay entries: %d\n", status);
      return status;
    }
  } else {
    FS_TRACE_DEBUG("replay: Not replaying entries\n");
  }

  // Update to the new sequence_number.
  journal_superblock.Update(journal_superblock.start(), sequence_number);

  if (out_journal_superblock) {
    *out_journal_superblock = std::move(journal_superblock);
  }
  return ZX_OK;
}

}  // namespace blobfs
