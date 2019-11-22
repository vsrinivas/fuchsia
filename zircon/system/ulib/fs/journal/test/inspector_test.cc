// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <fs/inspectable.h>
#include <fs/journal/format.h>
#include <fs/journal/inspector_journal.h>
#include <zxtest/zxtest.h>

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

const uint32_t kHeaderBlockOffset = 1;

const JournalCommitBlock kJournalCommitBlock = {
    .prefix =
        {
            .magic = kJournalEntryMagic,
            .sequence_number = kSequenceNumber,
            .flags = kJournalPrefixFlagCommit,
            .reserved = 0,
        },
    .checksum = kFakeChecksum,
};
const uint32_t kCommitBlockOffset = kHeaderBlockOffset + kPayloadBlocks + 1;

const JournalHeaderBlock kJournalRevocationBlock = {
    .prefix =
        {
            .magic = kJournalEntryMagic,
            .sequence_number = kSequenceNumber,
            .flags = kJournalPrefixFlagRevocation,
            .reserved = 0,
        },
    .payload_blocks = kPayloadBlocks,
    .target_blocks = {kTargetBlock1, kTargetBlock2},
    .target_flags = {0},
    .reserved = 0,
};
const uint32_t kRevocationBlockOffset = kCommitBlockOffset + 1;

class FakeInspectableJournal : public fs::Inspectable {
 public:
  explicit FakeInspectableJournal(uint32_t entry_count) {
    JournalInfo *info = reinterpret_cast<JournalInfo *>(buffer_);
    *info = kJournalInfo;
    entry_count_ = entry_count;

    auto *header =
        reinterpret_cast<JournalHeaderBlock *>(&buffer_[kHeaderBlockOffset * kBlockSize]);
    *header = kJournalHeaderBlock;

    auto *commit =
        reinterpret_cast<JournalCommitBlock *>(&buffer_[kCommitBlockOffset * kBlockSize]);
    *commit = kJournalCommitBlock;

    auto *revocation =
        reinterpret_cast<JournalHeaderBlock *>(&buffer_[kRevocationBlockOffset * kBlockSize]);
    *revocation = kJournalRevocationBlock;
  }

  zx_status_t ReadBlock(blk_t start_block_num, void *out_data) const {
    EXPECT_LT(start_block_num, kCapacity);
    memcpy(out_data, &buffer_[start_block_num * kBlockSize], kBlockSize);
    return ZX_OK;
  }

 private:
  uint8_t buffer_[kBlockSize * kCapacity] = {};
  uint32_t entry_count_ = 0;
};

TEST(JournalInspector, JournalObject) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);

  EXPECT_STR_EQ(fs::kJournalName, journalObj.GetName());
  ASSERT_EQ(fs::kJournalNumElements, journalObj.GetNumElements());

  const uint64_t *value;
  size_t size;

  // Check if journal magic is correct.
  auto magic = journalObj.GetElementAt(0);
  ASSERT_STR_EQ(magic->GetName(), "magic");
  magic->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(info.magic));
  ASSERT_EQ(*value, info.magic);

  // Check if journal start_block is correct.
  auto start_block = journalObj.GetElementAt(1);
  ASSERT_STR_EQ(start_block->GetName(), "start_block");
  start_block->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(info.start_block));
  ASSERT_EQ(*value, info.start_block);

  // Check if journal reserved is correct.
  auto reserved = journalObj.GetElementAt(2);
  ASSERT_STR_EQ(reserved->GetName(), "reserved");
  reserved->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(info.reserved));
  ASSERT_EQ(*value, info.reserved);

  // Check if journal timestamp is correct.
  auto timestamp = journalObj.GetElementAt(3);
  ASSERT_STR_EQ(timestamp->GetName(), "timestamp");
  timestamp->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(info.timestamp));
  ASSERT_EQ(*value, info.timestamp);

  // Check if journal checksum is correct.
  auto checksum = journalObj.GetElementAt(4);
  ASSERT_STR_EQ(checksum->GetName(), "checksum");
  checksum->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(info.checksum));
  ASSERT_EQ(*value, info.checksum);

  // Check if journal entries count is correct is correct.
  auto entries = journalObj.GetElementAt(5);
  ASSERT_STR_EQ(entries->GetName(), kJournalEntriesName);
  ASSERT_EQ(kCapacity - kJournalMetadataBlocks, entries->GetNumElements());
}

TEST(JournalInspector, EntriesNumOfElements) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);
  auto entries = journalObj.GetElementAt(5);

  ASSERT_EQ(kCapacity - kJournalMetadataBlocks, entries->GetNumElements());
}

TEST(JournalInspector, EntriesBlocks) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);
  auto entries = journalObj.GetElementAt(5);
  char str[1024];

  for (uint32_t i = 0; i < entries->GetNumElements(); i++) {
    auto entry = entries->GetElementAt(i);
    switch (i) {
      case kHeaderBlockOffset - kJournalMetadataBlocks:
        snprintf(str, sizeof(str), "Journal[%u]: Header", i);
        break;
      case kCommitBlockOffset - kJournalMetadataBlocks:
        snprintf(str, sizeof(str), "Journal[%u]: Commit", i);
        break;
      case kRevocationBlockOffset - kJournalMetadataBlocks:
        snprintf(str, sizeof(str), "Journal[%u]: Revocation", i);
        break;
      default:
        snprintf(str, sizeof(str), "Journal[%u]: Block", i);
        break;
    }
    ASSERT_STR_EQ(str, entry->GetName());
  }
}

TEST(JournalInspector, EntryHeader) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);
  auto entries = journalObj.GetElementAt(5);

  auto entry = entries->GetElementAt(kHeaderBlockOffset - kJournalMetadataBlocks);

  const uint64_t *value;
  size_t size;

  // Check if journal magic is correct.
  auto magic = entry->GetElementAt(0);
  ASSERT_STR_EQ(magic->GetName(), "magic");
  magic->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalEntryMagic);

  auto sequence_number = entry->GetElementAt(1);
  ASSERT_STR_EQ(sequence_number->GetName(), "sequence number");
  sequence_number->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kSequenceNumber);

  auto flags = entry->GetElementAt(2);
  ASSERT_STR_EQ(flags->GetName(), "flags");
  flags->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalPrefixFlagHeader);

  auto reserved = entry->GetElementAt(3);
  ASSERT_STR_EQ(reserved->GetName(), "reserved");
  reserved->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, 0);

  auto payload_blocks = entry->GetElementAt(4);
  ASSERT_STR_EQ(payload_blocks->GetName(), "payload blocks");
  payload_blocks->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kPayloadBlocks);

  auto target_block = entry->GetElementAt(5);
  ASSERT_STR_EQ(target_block->GetName(), "target block");
  target_block->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kTargetBlock1);

  target_block = entry->GetElementAt(6);
  ASSERT_STR_EQ(target_block->GetName(), "target block");
  target_block->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kTargetBlock2);
}

TEST(JournalInspector, EntryCommit) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);
  auto entries = journalObj.GetElementAt(5);

  auto entry = entries->GetElementAt(kCommitBlockOffset - kJournalMetadataBlocks);

  const uint64_t *value;
  size_t size;

  // Check if journal magic is correct.
  auto magic = entry->GetElementAt(0);
  ASSERT_STR_EQ(magic->GetName(), "magic");
  magic->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalEntryMagic);

  auto sequence_number = entry->GetElementAt(1);
  ASSERT_STR_EQ(sequence_number->GetName(), "sequence number");
  sequence_number->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kSequenceNumber);

  auto flags = entry->GetElementAt(2);
  ASSERT_STR_EQ(flags->GetName(), "flags");
  flags->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalPrefixFlagCommit);

  auto reserved = entry->GetElementAt(3);
  ASSERT_STR_EQ(reserved->GetName(), "reserved");
  reserved->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, 0);
}

TEST(JournalInspector, EntryRevocationRecord) {
  FakeInspectableJournal fake_journal(3);
  JournalInfo info = kJournalInfo;
  JournalObject journalObj(info, 0, kCapacity, &fake_journal);
  auto entries = journalObj.GetElementAt(5);

  auto entry = entries->GetElementAt(kRevocationBlockOffset - kJournalMetadataBlocks);

  const uint64_t *value;
  size_t size;

  // Check if journal magic is correct.
  auto magic = entry->GetElementAt(0);
  ASSERT_STR_EQ(magic->GetName(), "magic");
  magic->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalEntryMagic);

  auto sequence_number = entry->GetElementAt(1);
  ASSERT_STR_EQ(sequence_number->GetName(), "sequence number");
  sequence_number->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kSequenceNumber);

  auto flags = entry->GetElementAt(2);
  ASSERT_STR_EQ(flags->GetName(), "flags");
  flags->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, kJournalPrefixFlagRevocation);

  auto reserved = entry->GetElementAt(3);
  ASSERT_STR_EQ(reserved->GetName(), "reserved");
  reserved->GetValue(reinterpret_cast<const void **>(&value), &size);
  ASSERT_EQ(size, sizeof(uint64_t));
  ASSERT_EQ(*value, 0);
}

}  // namespace
}  // namespace fs
