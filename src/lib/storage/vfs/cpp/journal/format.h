// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of a journal.

#ifndef FS_JOURNAL_FORMAT_H_
#define FS_JOURNAL_FORMAT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

#include <fbl/algorithm.h>
#include <fbl/macros.h>

namespace fs {

constexpr uint64_t kJournalBlockSize = 8192;

// Number of metadata blocks allocated for the whole journal: 1 info block.
constexpr uint32_t kJournalMetadataBlocks = 1;
// Number of metadata blocks allocated for each entry: 2 (header block, commit block).
constexpr uint32_t kJournalEntryHeaderBlocks = 1;
constexpr uint32_t kJournalEntryCommitBlocks = 1;
constexpr uint32_t kEntryMetadataBlocks = kJournalEntryHeaderBlocks + kJournalEntryCommitBlocks;

constexpr uint64_t kJournalMagic = 0x626c6f626a726e6cULL;

struct JournalInfo {
  uint64_t magic;
  uint64_t start_block;  // Block of first journal entry (relative to entries start).
  uint64_t reserved;     // Unused.
  uint64_t timestamp;    // Timestamp at which the info block was last written.
  uint32_t checksum;     // crc32 checksum of the preceding contents of the info block.
};

static_assert(sizeof(JournalInfo) <= kJournalBlockSize, "Journal info size is too large");

constexpr uint64_t kJournalEntryMagic = 0x696d616a75726e6cULL;

constexpr uint64_t kJournalPrefixFlagHeader = 1;
constexpr uint64_t kJournalPrefixFlagCommit = 2;
constexpr uint64_t kJournalPrefixFlagRevocation = 3;
constexpr uint64_t kJournalPrefixFlagMask = 0xF;

enum class JournalObjectType {
  kUnknown = 0,
  kHeader,
  kCommit,
  kRevocation,
};

// The prefix structure on both header blocks and commit blocks.
struct JournalPrefix {
  JournalObjectType ObjectType() const {
    switch (flags & kJournalPrefixFlagMask) {
      case kJournalPrefixFlagHeader:
        return JournalObjectType::kHeader;
      case kJournalPrefixFlagCommit:
        return JournalObjectType::kCommit;
      case kJournalPrefixFlagRevocation:
        return JournalObjectType::kRevocation;
      default:
        return JournalObjectType::kUnknown;
    }
  }

  // Must be |kJournalMagic|.
  uint64_t magic;
  // A monotonically increasing value. This entry will only be replayed if the JournalInfo
  // block contains a sequence number less than or equal to this value.
  uint64_t sequence_number;
  // Identifies the type of this journal object. See |GetJournalObjectType()|.
  uint64_t flags;
  uint64_t reserved;
};

// The maximum number of blocks which fit within a |JournalHeaderBlock|.
constexpr uint32_t kMaxBlockDescriptors = 679;

// Flags for JournalHeaderBlock.target_flags:

// Identifies that the journaled block begins with |kJournalEntryMagic|, which are replaced
// with zeros to avoid confusing replay logic.
constexpr uint32_t kJournalBlockDescriptorFlagEscapedBlock = 1;

struct JournalHeaderBlock {
  JournalPrefix prefix;
  // The number of blocks between this header and the following commit block.
  // [0, payload_blocks) are valid indices for |target_blocks| and |target_flags|.
  uint64_t payload_blocks;
  // The final location of the blocks within the payload.
  uint64_t target_blocks[kMaxBlockDescriptors];
  // Flags about each block within the payload.
  uint32_t target_flags[kMaxBlockDescriptors];
  uint32_t reserved;
};
static_assert(sizeof(JournalHeaderBlock) == kJournalBlockSize, "Invalid Header Block size");

struct JournalCommitBlock {
  JournalPrefix prefix;
  // CRC32 checksum of all prior blocks (not including commit block itself).
  uint32_t checksum;
};
static_assert(sizeof(JournalCommitBlock) <= kJournalBlockSize, "Commit Block is too large");

}  // namespace fs

#endif  // FS_JOURNAL_FORMAT_H_
