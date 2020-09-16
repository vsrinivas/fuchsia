// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <map>

#include <fs/journal/replay.h>
#include <gtest/gtest.h>

#include "entry_view.h"

namespace fs {
namespace {

const vmoid_t kInfoVmoid = 1;
const vmoid_t kJournalVmoid = 2;
const vmoid_t kOtherVmoid = 3;
const size_t kJournalLength = 10;
const uint32_t kBlockSize = 8192;
const uint64_t kGoldenSequenceNumber = 1337;

class MockVmoidRegistry : public storage::VmoidRegistry {
 public:
  ~MockVmoidRegistry() { ZX_ASSERT(vmos_.empty()); }

  const zx::vmo& GetVmo(vmoid_t vmoid) const { return *(vmos_.at(vmoid)); }

  void SetNextVmoid(vmoid_t vmoid) { next_vmoid_ = vmoid; }

 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) override {
    vmos_.emplace(std::make_pair(next_vmoid_, zx::unowned_vmo(vmo.get())));
    *out = storage::Vmoid(next_vmoid_++);
    return ZX_OK;
  }

  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final {
    vmos_.erase(vmoid.TakeId());
    return ZX_OK;
  }

  vmoid_t next_vmoid_ = BLOCK_VMOID_INVALID;
  std::map<vmoid_t, zx::unowned_vmo> vmos_;
};

class ParseJournalTestFixture : public testing::Test {
 public:
  virtual ~ParseJournalTestFixture() = default;

  void SetUp() override {
    auto info_block_buffer = std::make_unique<storage::VmoBuffer>();
    registry_.SetNextVmoid(kInfoVmoid);
    ASSERT_EQ(info_block_buffer->Initialize(&registry_, 1, kBlockSize, "info-block"), ZX_OK);
    info_block_ = JournalSuperblock(std::move(info_block_buffer));

    registry_.SetNextVmoid(kJournalVmoid);
    ASSERT_EQ(journal_buffer_.Initialize(&registry_, kJournalLength, kBlockSize, "journal"), ZX_OK);

    registry_.SetNextVmoid(kOtherVmoid);
  }

  JournalSuperblock* info_block() { return &info_block_; }
  storage::VmoBuffer* journal_buffer() { return &journal_buffer_; }
  MockVmoidRegistry* registry() { return &registry_; }

 private:
  MockVmoidRegistry registry_;
  JournalSuperblock info_block_;
  storage::VmoBuffer journal_buffer_;
};

using ParseJournalTest = ParseJournalTestFixture;

TEST_F(ParseJournalTest, BadJournalChecksumExpectError) {
  // Don't bother setting the checksum on the info block.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ZX_ERR_IO, ParseJournalEntries(info_block(), journal_buffer(), &operations,
                                           &sequence_number, &next_entry_start));
}

TEST_F(ParseJournalTest, BadJournalStartExpectError) {
  // Set the start field to a too-large value.
  uint64_t start = journal_buffer()->capacity();
  info_block()->Update(start, 0);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start));
}

TEST_F(ParseJournalTest, EmptyJournalNoOperations) {
  info_block()->Update(0, 0);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  EXPECT_EQ(operations.size(), 0ul);
  EXPECT_EQ(sequence_number, 0ul);
  EXPECT_EQ(0ul, next_entry_start);
}

TEST_F(ParseJournalTest, EmptyJournalNonzeroSequenceNumber) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  EXPECT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

void AddOperation(uint64_t dev_offset, uint64_t length,
                  std::vector<storage::BufferedOperation>* operations) {
  storage::BufferedOperation operation;
  operation.op.type = storage::OperationType::kWrite;
  operation.op.dev_offset = dev_offset;
  operation.op.length = length;
  operations->push_back(std::move(operation));
}

void CheckWriteOperation(const storage::BufferedOperation& operation, uint64_t vmo_offset,
                         uint64_t dev_offset, uint64_t length) {
  EXPECT_EQ(kJournalVmoid, operation.vmoid);
  EXPECT_EQ(storage::OperationType::kWrite, operation.op.type);
  EXPECT_EQ(vmo_offset, operation.op.vmo_offset);
  EXPECT_EQ(dev_offset, operation.op.dev_offset);
  EXPECT_EQ(length, operation.op.length);
}

TEST_F(ParseJournalTest, OneEntryOneOperation) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(kEntryLength, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
}

TEST_F(ParseJournalTest, OneEntryOneOperationFullJournal) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  const uint64_t kDevOffset = 10;  // Arbitrary
  const uint64_t kLength = kJournalLength - kEntryMetadataBlocks;
  AddOperation(kDevOffset, kLength, &ops);

  const uint64_t kEntryLength = kLength + kEntryMetadataBlocks;
  static_assert(kEntryLength == kJournalLength, "Attempting to test full journal");
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, kDevOffset, kLength));
}

TEST_F(ParseJournalTest, OneEntryOneOperationWrapsAroundJournal) {
  // Start writing two blocks before the end of the journal.
  uint64_t vmo_offset = kJournalLength - 2;
  info_block()->Update(vmo_offset, kGoldenSequenceNumber);

  // This operation will be split as follows:
  //   [ 2, 3, 4, C, _, _, _, _, _, H, 1 ]
  //
  // Resulting in two writeback operations:
  //   [ _, _, _, _, _, _, _, _, _, _, 1 ], and
  //   [ 2, 3, 4, _, _, _, _, _, _, _, _ ]
  std::vector<storage::BufferedOperation> ops;
  uint64_t dev_offset = 10;
  const uint64_t kOperationLength = 4;
  AddOperation(dev_offset, kOperationLength, &ops);

  const uint64_t kEntryLength = kOperationLength + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), vmo_offset, kEntryLength),
                              ops, kGoldenSequenceNumber);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 2ul);
  vmo_offset += kJournalEntryHeaderBlocks;

  uint64_t length = kJournalLength - vmo_offset;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, dev_offset, length));

  dev_offset += length;
  vmo_offset = 0;
  length = kOperationLength - length;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[1], vmo_offset, dev_offset, length));

  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  ASSERT_EQ(vmo_offset + length + kJournalEntryCommitBlocks, next_entry_start);
}

TEST_F(ParseJournalTest, OneEntryManyOperations) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 3, &ops);
  AddOperation(/* dev_offset= */ 20, /* length= */ 2, &ops);
  AddOperation(/* dev_offset= */ 30, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 6 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  EXPECT_EQ(operations.size(), 3ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(kEntryLength, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 3));
  vmo_offset += 3;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[1], vmo_offset, 20, 2));
  vmo_offset += 2;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[2], vmo_offset, 30, 1));
}

TEST_F(ParseJournalTest, MultipleEntriesDifferentDevOffsetCausesTwoEntriesParsed) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
  const uint64_t kEntryLengthA = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view_a(storage::BlockBufferView(journal_buffer(), 0, kEntryLengthA), ops,
                                kGoldenSequenceNumber);

  ops.clear();
  AddOperation(/* dev_offset= */ 20, /* length= */ 3, &ops);
  const uint64_t kEntryLengthB = 3 + kEntryMetadataBlocks;
  JournalEntryView entry_view_b(
      storage::BlockBufferView(journal_buffer(), kEntryLengthA, kEntryLengthB), ops,
      kGoldenSequenceNumber + 1);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 2ul);
  EXPECT_EQ(kGoldenSequenceNumber + 2, sequence_number);
  EXPECT_EQ(kEntryLengthA + kEntryLengthB, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
  vmo_offset += kEntryLengthA;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[1], vmo_offset, 20, 3));
}

TEST_F(ParseJournalTest, MultipleEntriesSameDevOffsetCausesOneEntryParsed) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
  const uint64_t kEntryLengthA = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view_a(storage::BlockBufferView(journal_buffer(), 0, kEntryLengthA), ops,
                                kGoldenSequenceNumber);

  ops.clear();
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
  const uint64_t kEntryLengthB = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view_b(
      storage::BlockBufferView(journal_buffer(), kEntryLengthA, kEntryLengthB), ops,
      kGoldenSequenceNumber + 1);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 2, sequence_number);
  EXPECT_EQ(kEntryLengthA + kEntryLengthB, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks + kEntryLengthA;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
}

// Tests that contiguous entries with a non-increasing sequence number will
// be discarded. In a functioning journal, each subsequent entry will have exclusively
// incrementing sequence numbers, and deviation from that behavior will imply "invalid
// journal metadata" that should be discarded. This tests one of those deviations (sequence number
// is not incremented), and validates that the bad entry is ignored.
TEST_F(ParseJournalTest, MultipleEntriesWithSameSequenceNumberOnlyKeepsFirst) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
  const uint64_t kEntryLengthA = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view_a(storage::BlockBufferView(journal_buffer(), 0, kEntryLengthA), ops,
                                kGoldenSequenceNumber);

  ops.clear();
  AddOperation(/* dev_offset= */ 20, /* length= */ 3, &ops);
  const uint64_t kEntryLengthB = 3 + kEntryMetadataBlocks;
  JournalEntryView entry_view_b(
      storage::BlockBufferView(journal_buffer(), kEntryLengthA, kEntryLengthB), ops,
      kGoldenSequenceNumber);

  // Writing entries with the same sequence number only parses the first.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  ASSERT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
}

TEST_F(ParseJournalTest, EscapedEntry) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;

  // Create an "escaped" entry.
  storage::BlockBufferView view(journal_buffer(), 1, 1);
  auto ptr = static_cast<uint64_t*>(view.Data(0));
  ptr[0] = kJournalEntryMagic;
  ptr[1] = 0xDEADBEEF;

  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Verify that it was escaped.
  const auto& const_entry_view = entry_view;
  EXPECT_TRUE(const_entry_view.header().EscapedBlock(0));
  EXPECT_EQ(ptr[0], 0ul);
  EXPECT_EQ(0xDEADBEEF, ptr[1]);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(kEntryLength, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));

  // Verify that the entry is un-escaped after parsing.
  EXPECT_EQ(ptr[0], kJournalEntryMagic);
  EXPECT_EQ(ptr[1], 0xDEADBEEFul);
}

TEST_F(ParseJournalTest, TooOldDropped) {
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Move the info block past this counter, but in the same location.
  info_block()->Update(0, kGoldenSequenceNumber + 1);

  // Observe that the new sequence_number is parsed, but the entry is dropped.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

TEST_F(ParseJournalTest, NewerThanExpectedDropped) {
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Move the info block backwards in time.
  const uint64_t kUpdatedSequenceNumber = kGoldenSequenceNumber - 1;
  info_block()->Update(0, kUpdatedSequenceNumber);

  // Observe that the entry's sequence_number is parsed as too new, and dropped.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  EXPECT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kUpdatedSequenceNumber, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

TEST_F(ParseJournalTest, EntryMultipleTimes) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Observe that we can replay journal entries with this setup.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(kEntryLength, next_entry_start);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
  operations.clear();

  // We can replay the same entries multiple times.
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  EXPECT_EQ(kEntryLength, next_entry_start);
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
}

TEST_F(ParseJournalTest, EntryModifiedHeaderDropped) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Before we replay, flip some bits in the header.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  JournalHeaderView raw_block(
      fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(buffer_view.Data(0)), buffer_view.BlockSize()));
  raw_block.SetTargetBlock(16, ~(raw_block.TargetBlock(16)));

  // As a result, there are no entries identified as replayable.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

TEST_F(ParseJournalTest, EntryModifiedEntryDropped) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Before we replay, flip some bits in the entry.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  auto raw_bytes = static_cast<uint8_t*>(buffer_view.Data(1));
  raw_bytes[0] = static_cast<uint8_t>(~raw_bytes[0]);

  // As a result, there are no entries identified as replayable.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

TEST_F(ParseJournalTest, EntryModifiedCommitDropped) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Before we replay, flip some bits in the commit.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  auto raw_commit = static_cast<JournalCommitBlock*>(buffer_view.Data(2));
  raw_commit->prefix.sequence_number++;

  // As a result, there are no entries identified as replayable.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber, sequence_number);
  EXPECT_EQ(next_entry_start, 0ul);
}

TEST_F(ParseJournalTest, EntryModifiedAfterCommitStillKept) {
  info_block()->Update(0, kGoldenSequenceNumber);
  std::vector<storage::BufferedOperation> ops;
  AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);

  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                              kGoldenSequenceNumber);

  // Before we replay, flip some bits in the commit block.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  auto raw_bytes = static_cast<uint8_t*>(buffer_view.Data(2));
  // Intentionally flip bits AFTER the commit structure itself, but still in the
  // same block.
  size_t index = sizeof(JournalCommitBlock) + 1;
  raw_bytes[index] = static_cast<uint8_t>(~raw_bytes[index]);

  // The current implementation of journaling is not checksumming the commit block.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  ASSERT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 1ul);
  EXPECT_EQ(kGoldenSequenceNumber + 1, sequence_number);
  uint64_t vmo_offset = kJournalEntryHeaderBlocks;
  ASSERT_NO_FATAL_FAILURE(CheckWriteOperation(operations[0], vmo_offset, 10, 1));
}

TEST_F(ParseJournalTest, DetectsCorruptJournalIfOldEntryHasBadChecksumButGoodLength) {
  info_block()->Update(0, kGoldenSequenceNumber);
  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  // Place two entries into the journal.
  {
    std::vector<storage::BufferedOperation> ops;
    AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
    JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                                kGoldenSequenceNumber);
  }
  {
    std::vector<storage::BufferedOperation> ops;
    AddOperation(/* dev_offset= */ 20, /* length= */ 1, &ops);
    JournalEntryView entry_view(
        storage::BlockBufferView(journal_buffer(), kEntryLength, kEntryLength), ops,
        kGoldenSequenceNumber + 1);
  }

  // Before we replay, flip some bits in the old entry's header.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  JournalHeaderView raw_block(
      fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(buffer_view.Data(0)), buffer_view.BlockSize()));
  raw_block.SetTargetBlock(16, ~(raw_block.TargetBlock(16)));

  // As a result, there are no entries identified as replayable, and
  // (because the second entry was valid, but the first entry wasn't) the journal
  // is identified as corrupt.
  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start));
}

TEST_F(ParseJournalTest, DoesntDetectCorruptJournalIfOldEntryHasBadChecksumAndBadLength) {
  info_block()->Update(0, kGoldenSequenceNumber);
  const uint64_t kEntryLength = 1 + kEntryMetadataBlocks;
  // Place two entries into the journal.
  {
    std::vector<storage::BufferedOperation> ops;
    AddOperation(/* dev_offset= */ 10, /* length= */ 1, &ops);
    JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), 0, kEntryLength), ops,
                                kGoldenSequenceNumber);
  }
  {
    std::vector<storage::BufferedOperation> ops;
    AddOperation(/* dev_offset= */ 20, /* length= */ 1, &ops);
    JournalEntryView entry_view(
        storage::BlockBufferView(journal_buffer(), kEntryLength, kEntryLength), ops,
        kGoldenSequenceNumber + 1);
  }

  // Before we replay, flip some bits in the old entry's header.
  //
  // This time, flip the number of blocks to be replayed, so the subsequent entry
  // cannot be located.
  storage::BlockBufferView buffer_view(journal_buffer(), 0, kEntryLength);
  auto raw_block = static_cast<JournalHeaderBlock*>(buffer_view.Data(0));
  raw_block->payload_blocks = ~(raw_block->payload_blocks);

  std::vector<storage::BufferedOperation> operations;
  uint64_t sequence_number = 0;
  uint64_t next_entry_start = 0;
  EXPECT_EQ(ParseJournalEntries(info_block(), journal_buffer(), &operations, &sequence_number,
                                &next_entry_start),
            ZX_OK);
  ASSERT_EQ(operations.size(), 0ul);
  EXPECT_EQ(kGoldenSequenceNumber, sequence_number);
}

class MockTransactionHandler final : public fs::TransactionHandler {
 public:
  using TransactionCallback =
      fit::function<zx_status_t(const std::vector<storage::BufferedOperation>& requests)>;

  MockTransactionHandler() = default;
  MockTransactionHandler(TransactionCallback* callbacks, size_t transactions_expected)
      : callbacks_(callbacks), transactions_expected_(transactions_expected) {}

  ~MockTransactionHandler() { EXPECT_EQ(transactions_expected_, transactions_seen_); }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& requests) override {
    EXPECT_LT(transactions_seen_, transactions_expected_);
    if (transactions_seen_ == transactions_expected_) {
      return ZX_ERR_BAD_STATE;
    }
    return callbacks_[transactions_seen_++](requests);
  }

 private:
  TransactionCallback* callbacks_ = nullptr;
  size_t transactions_expected_ = 0;
  size_t transactions_seen_ = 0;
};

class ReplayJournalTest : public ParseJournalTestFixture {
 public:
  static constexpr uint64_t kJournalAreaStart = 5;
  static constexpr uint64_t kJournalAreaLength = kJournalLength + kJournalMetadataBlocks;
  static constexpr uint64_t kJournalEntryStart = kJournalAreaStart + kJournalMetadataBlocks;
  static constexpr uint64_t kJournalEntryLength = kJournalLength;

  void ValidInfoReadRequest(const storage::BufferedOperation& request) const {
    EXPECT_EQ(storage::OperationType::kRead, request.op.type);
    EXPECT_EQ(request.op.vmo_offset, 0ul);
    EXPECT_EQ(kJournalAreaStart, request.op.dev_offset);
    EXPECT_EQ(kJournalMetadataBlocks, request.op.length);
  }

  void ValidInfoWriteRequest(const storage::BufferedOperation& request) const {
    EXPECT_EQ(storage::OperationType::kWrite, request.op.type);
    EXPECT_EQ(request.op.vmo_offset, 0ul);
    EXPECT_EQ(kJournalAreaStart, request.op.dev_offset);
    EXPECT_EQ(kJournalMetadataBlocks, request.op.length);
  }

  void ValidEntriesReadRequest(const storage::BufferedOperation& request) const {
    EXPECT_EQ(storage::OperationType::kRead, request.op.type);
    EXPECT_EQ(request.op.vmo_offset, 0ul);
    EXPECT_EQ(kJournalEntryStart, request.op.dev_offset);
    EXPECT_EQ(kJournalEntryLength, request.op.length);
  }

  // Take the contents of the pre-registered journal superblock and transfer
  // it into the requested vmoid.
  void TransferInfoTo(vmoid_t vmoid) {
    char buf[kBlockSize * kJournalMetadataBlocks];
    EXPECT_EQ(registry()->GetVmo(kInfoVmoid).read(buf, 0, sizeof(buf)), ZX_OK);
    EXPECT_EQ(registry()->GetVmo(vmoid).write(buf, 0, sizeof(buf)), ZX_OK);
  }

  // Take the contents of the pre-registered journal buffer and transfer
  // it into the requested vmoid.
  void TransferEntryTo(vmoid_t vmoid, size_t offset, uint64_t length) {
    char entry_buf[kBlockSize * length];
    EXPECT_EQ(
        registry()->GetVmo(kJournalVmoid).read(entry_buf, offset * kBlockSize, sizeof(entry_buf)),
        ZX_OK);
    EXPECT_EQ(registry()->GetVmo(vmoid).write(entry_buf, offset * kBlockSize, sizeof(entry_buf)),
              ZX_OK);
  }
};

TEST_F(ReplayJournalTest, BadJournalSuperblockFails) {
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // Return OK, but don't provide any values. This should fail during replay.
        EXPECT_GE(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kRead, requests[0].op.type);
        return ZX_OK;
      },
  };
  MockTransactionHandler transaction_handler(callbacks, std::size(callbacks));
  JournalSuperblock superblock;
  ASSERT_EQ(ZX_ERR_IO, ReplayJournal(&transaction_handler, registry(), kJournalAreaStart,
                                     kJournalAreaLength, kBlockSize, &superblock));
}

TEST_F(ReplayJournalTest, CannotReadJournalFails) {
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_GE(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kRead, requests[0].op.type);
        return ZX_ERR_IO;
      },
  };
  MockTransactionHandler transaction_handler(callbacks, std::size(callbacks));
  JournalSuperblock superblock;
  ASSERT_EQ(ZX_ERR_IO, ReplayJournal(&transaction_handler, registry(), kJournalAreaStart,
                                     kJournalAreaLength, kBlockSize, &superblock));
}

TEST_F(ReplayJournalTest, EmptyJournalDoesNothing) {
  // Fill the preregistered info block with valid data.
  constexpr uint64_t kStart = 1;
  constexpr uint64_t kSequenceNumber = 3;
  info_block()->Update(kStart, kSequenceNumber);

  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // First request: Reading from the journal.
        EXPECT_EQ(requests.size(), 2ul);
        ValidInfoReadRequest(requests[0]);
        ValidEntriesReadRequest(requests[1]);

        // Transfer pre-seeded info block, but nothing else.
        TransferInfoTo(requests[0].vmoid);
        return ZX_OK;
      },
  };
  MockTransactionHandler transaction_handler(callbacks, std::size(callbacks));
  JournalSuperblock superblock;
  ASSERT_EQ(ReplayJournal(&transaction_handler, registry(), kJournalAreaStart, kJournalAreaLength,
                          kBlockSize, &superblock),
            ZX_OK);
  EXPECT_EQ(kStart, superblock.start());
  EXPECT_EQ(kSequenceNumber, superblock.sequence_number());
}

TEST_F(ReplayJournalTest, OneEntry) {
  // Fill the pre-registered info block with valid data.
  constexpr uint64_t kStart = 0;
  constexpr uint64_t kSequenceNumber = 3;
  info_block()->Update(kStart, kSequenceNumber);

  // Fill the pre-registered journal buffer with one entry.
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 1234,
              .length = 1,
          },
      },
  };
  uint64_t entry_size = operations[0].op.length + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), kStart, entry_size),
                              operations, kSequenceNumber);

  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // First request: Reading from the journal.
        // Transfer the pre-seeded info block.
        EXPECT_EQ(requests.size(), 2ul);
        ValidInfoReadRequest(requests[0]);
        ValidEntriesReadRequest(requests[1]);

        // Transfer the pre-seeded journal entry.
        TransferInfoTo(requests[0].vmoid);
        TransferEntryTo(requests[1].vmoid, kStart, entry_size);
        return ZX_OK;
      },
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // Observe that the replay code replays the provided operation.
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kJournalEntryHeaderBlocks, requests[0].op.vmo_offset);
        EXPECT_EQ(operations[0].op.dev_offset, requests[0].op.dev_offset);
        EXPECT_EQ(operations[0].op.length, requests[0].op.length);
        return ZX_OK;
      },
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // Observe that the replay code updates the journal superblock.
        EXPECT_EQ(requests.size(), 1ul);
        ValidInfoWriteRequest(requests[0]);
        return ZX_OK;
      }};

  MockTransactionHandler transaction_handler(callbacks, std::size(callbacks));
  JournalSuperblock superblock;
  ASSERT_EQ(ReplayJournal(&transaction_handler, registry(), kJournalAreaStart, kJournalAreaLength,
                          kBlockSize, &superblock),
            ZX_OK);
  EXPECT_EQ(kStart + entry_size, superblock.start());
  // The sequence_number should have advanced to avoid replaying the old entry.
  EXPECT_EQ(kSequenceNumber + 1, superblock.sequence_number());
}

TEST_F(ReplayJournalTest, CannotWriteParsedEntriesFails) {
  // Fill the pre-registered info block with valid data.
  constexpr uint64_t kStart = 0;
  constexpr uint64_t kSequenceNumber = 3;
  info_block()->Update(kStart, kSequenceNumber);

  // Fill the pre-registered journal buffer with one entry.
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 1234,
              .length = 1,
          },
      },
  };
  uint64_t entry_size = operations[0].op.length + kEntryMetadataBlocks;
  JournalEntryView entry_view(storage::BlockBufferView(journal_buffer(), kStart, entry_size),
                              operations, kSequenceNumber);

  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // First request: Reading from the journal.
        // Transfer the pre-seeded info block.
        EXPECT_EQ(requests.size(), 2ul);
        ValidInfoReadRequest(requests[0]);
        ValidEntriesReadRequest(requests[1]);

        // Transfer the pre-seeded journal entry.
        TransferInfoTo(requests[0].vmoid);
        TransferEntryTo(requests[1].vmoid, kStart, entry_size);
        return ZX_OK;
      },
      [&](const std::vector<storage::BufferedOperation>& requests) {
        // Observe that the replay code replays the provided operation, but return
        // an error instead.
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        return ZX_ERR_IO;
      }};
  MockTransactionHandler transaction_handler(callbacks, std::size(callbacks));
  JournalSuperblock superblock;
  ASSERT_EQ(ZX_ERR_IO, ReplayJournal(&transaction_handler, registry(), kJournalAreaStart,
                                     kJournalAreaLength, kBlockSize, &superblock));
}

}  // namespace
}  // namespace fs
