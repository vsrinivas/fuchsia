// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <fs/journal/format.h>
#include <fs/journal/internal/inspector_parser.h>
#include <gtest/gtest.h>
#include <storage/buffer/array_buffer.h>

namespace fs {
namespace {

// Number of blocks in the default buffer.
const size_t kCapacity = 10;
const uint32_t kBlockSize = 8192;

const uint64_t kSequenceNumber = 1;
const uint64_t kPayloadBlocks = 2;
const uint64_t kTargetBlock1 = 13;
const uint64_t kTargetBlock2 = 31;
const uint32_t kFakeChecksum = 1234;

const JournalInfo kJournalInfo = {
    .magic = kJournalMagic,
    .start_block = 0,
    .reserved = 0,
    .timestamp = 200,
    .checksum = kFakeChecksum,
};

const JournalHeaderBlock kJournalHeaderBlock = {
    .prefix =
        {
            .magic = kJournalEntryMagic,
            .sequence_number = kSequenceNumber,
            .flags = kJournalPrefixFlagHeader,
            .reserved = 0,
        },
    .payload_blocks = kPayloadBlocks,
    .target_blocks = {kTargetBlock1, kTargetBlock2},
    .target_flags = {0},
    .reserved = 0,
};

TEST(InspectorParser, ParseJournalInfo) {
  storage::ArrayBuffer buffer(kCapacity, kBlockSize);
  auto info = reinterpret_cast<JournalInfo *>(buffer.Data(0));
  *info = kJournalInfo;

  JournalInfo parsed_info = GetJournalSuperblock(&buffer);

  EXPECT_EQ(parsed_info.magic, kJournalInfo.magic);
  EXPECT_EQ(parsed_info.checksum, kJournalInfo.checksum);
  EXPECT_EQ(parsed_info.reserved, kJournalInfo.reserved);
  EXPECT_EQ(parsed_info.start_block, kJournalInfo.start_block);
  EXPECT_EQ(parsed_info.timestamp, kJournalInfo.timestamp);
}

TEST(InspectorParser, ParseEntryBlock) {
  storage::ArrayBuffer buffer(kCapacity, kBlockSize);

  for (uint32_t i = 0; i < kCapacity; ++i) {
    auto *header = reinterpret_cast<JournalHeaderBlock *>(buffer.Data(i));
    *header = kJournalHeaderBlock;
    // Set the sequence number to the block number to distinguish blocks.
    header->prefix.sequence_number = i;
  }

  for (uint32_t i = 0; i < kCapacity - kJournalMetadataBlocks; ++i) {
    std::array<uint8_t, kJournalBlockSize> data = GetBlockEntry(&buffer, i);
    auto *header = reinterpret_cast<JournalHeaderBlock *>(data.data());
    // Check magic for sanity that block is a journal header.
    EXPECT_EQ(header->prefix.magic, kJournalEntryMagic);
    // GetBlockEntry skips over journal info blocks so that is reflected in its sequence number.
    EXPECT_EQ(header->prefix.sequence_number, i + kJournalMetadataBlocks);
  }
}

}  // namespace
}  // namespace fs
