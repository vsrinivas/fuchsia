// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "entry_view.h"

#include <vector>

#include <gtest/gtest.h>

namespace fs {
namespace {

// Number of blocks in the default buffer.
const size_t kCapacity = 5;
const uint32_t kBlockSize = 8192;

// Create a new class which can be used to customize the JournalEntryView object.
class Buffer : public storage::BlockBuffer {
 public:
  size_t capacity() const final { return kCapacity; }
  uint32_t BlockSize() const final { return kBlockSize; }
  vmoid_t vmoid() const final { return BLOCK_VMOID_INVALID; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final { return &buffer_[index * kBlockSize]; }
  const void* Data(size_t index) const final { return &buffer_[index * kBlockSize]; }

 private:
  uint8_t buffer_[kBlockSize * kCapacity] = {};
};

class EntryViewFixture : public testing::Test {
 public:
  storage::BlockBufferView make_view(size_t length) {
    return storage::BlockBufferView(&buffer_, 0, length);
  }

 private:
  Buffer buffer_;
};

using EntryViewTest = EntryViewFixture;

TEST_F(EntryViewTest, CreateJournalEntryView) { JournalEntryView view(make_view(3)); }

TEST_F(EntryViewTest, SetHeaderFromOperation) {
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

  JournalEntryView view(make_view(3), operations, 1);
  const JournalEntryView& c_view = view;
  auto header = view.header();
  EXPECT_EQ(JournalObjectType::kHeader, header.ObjectType());
  EXPECT_EQ(JournalObjectType::kCommit, c_view.footer()->prefix.ObjectType());
  EXPECT_EQ(header.TargetBlock(0), 1234ul);
}

TEST_F(EntryViewTest, SetHeaderFromMultipleOperations) {
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
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 5678,
              .length = 1,
          },
      },
  };

  JournalEntryView view(make_view(4), operations, 1);
  auto header = view.header();
  EXPECT_EQ(header.TargetBlock(0), 1234ul);
  EXPECT_FALSE(header.EscapedBlock(0));
  EXPECT_EQ(header.TargetBlock(1), 5678ul);
  EXPECT_FALSE(header.EscapedBlock(1));
}

TEST_F(EntryViewTest, SameJournalEntryGeneratesSameChecksum) {
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 0,
              .length = 1,
          },
      },
  };

  JournalEntryView view(make_view(3), operations, 1);
  uint32_t checksum = view.CalculateChecksum();

  JournalEntryView view2(make_view(3), operations, 1);
  ASSERT_EQ(checksum, view2.CalculateChecksum());
}

TEST_F(EntryViewTest, DifferentTargetBlockGeneratesDifferentChecksum) {
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 0,
              .length = 1,
          },
      },
  };

  JournalEntryView view(make_view(3), operations, 1);
  uint32_t checksum = view.CalculateChecksum();

  // Change the target block.
  operations[0].op.dev_offset += 1;

  JournalEntryView view2(make_view(3), operations, 1);
  ASSERT_NE(checksum, view2.CalculateChecksum());
}

TEST_F(EntryViewTest, DifferentSequenceNumberGeneratesDifferentChecksum) {
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 0,
              .length = 1,
          },
      },
  };

  JournalEntryView view(make_view(3), operations, 1);
  uint32_t checksum = view.CalculateChecksum();

  JournalEntryView view2(make_view(3), operations, 2);
  // Change the sequence number.
  ASSERT_NE(checksum, view2.CalculateChecksum());
}

TEST_F(EntryViewTest, ChecksumDoesNotIncludeCommit) {
  std::vector<storage::BufferedOperation> operations = {
      {
          .vmoid = 0,
          {
              .type = storage::OperationType::kWrite,
              .vmo_offset = 0,
              .dev_offset = 0,
              .length = 1,
          },
      },
  };

  JournalEntryView view(make_view(3), operations, 1);
  uint32_t checksum = view.CalculateChecksum();

  JournalEntryView view2(make_view(3), operations, 1);

  // Change some data in the commit block.
  //
  // Note that we need to do some casting hackery to pull this off, because the journal
  // entry view only allows normal modification through the "set" method exclusively.
  const_cast<JournalCommitBlock*>(const_cast<const JournalEntryView*>(&view2)->footer())
      ->prefix.sequence_number++;
  ASSERT_EQ(checksum, view2.CalculateChecksum());
}

class EscapedEntryFixture : public EntryViewFixture {
 public:
  const uint64_t kTarget = 1234;

  std::vector<storage::BufferedOperation> operations() const {
    std::vector<storage::BufferedOperation> operations = {
        {
            .vmoid = 0,
            {
                .type = storage::OperationType::kWrite,
                .vmo_offset = 0,
                .dev_offset = kTarget,
                .length = 1,
            },
        },
    };
    return operations;
  }
};

using EntryViewEscapedTest = EscapedEntryFixture;

TEST_F(EntryViewEscapedTest, EscapedBlocksAreModifiedBySet) {
  auto buffer_view = make_view(3);
  uint64_t* ptr = static_cast<uint64_t*>(buffer_view.Data(kJournalEntryHeaderBlocks));
  // This value will be escaped.
  ptr[0] = kJournalEntryMagic;
  // This part of the block will be unmodified.
  ptr[1] = 0xDEADBEEF;

  JournalEntryView view(buffer_view, operations(), 1);
  auto header = view.header();

  EXPECT_TRUE(header.EscapedBlock(0));
  EXPECT_EQ(kTarget, header.TargetBlock(0));
  EXPECT_EQ(ptr[0], 0ul) << "Payload prefix should have been escaped, but it was not";
  EXPECT_EQ(0xDEADBEEF, ptr[1]) << "Remainder of payload should have remained unescaped";
}

TEST_F(EntryViewEscapedTest, EscapedBlocksCanBeDecoded) {
  auto buffer_view = make_view(3);
  uint64_t* ptr = static_cast<uint64_t*>(buffer_view.Data(kJournalEntryHeaderBlocks));
  // This value will be escaped.
  ptr[0] = kJournalEntryMagic;
  // This part of the block will be unmodified.
  ptr[1] = 0xDEADBEEF;

  JournalEntryView view(buffer_view, operations(), 1);

  EXPECT_EQ(ptr[0], 0ul) << "Payload prefix should have been escaped, but it was not";

  view.DecodePayloadBlocks();
  auto header = view.header();

  EXPECT_TRUE(header.EscapedBlock(0));
  EXPECT_EQ(kTarget, header.TargetBlock(0));
  EXPECT_EQ(kJournalEntryMagic, ptr[0]) << "Payload prefix should have been reset, but it was not";
  EXPECT_EQ(0xDEADBEEF, ptr[1]) << "Remainter of payload should have remained untouched";
}

}  // namespace
}  // namespace fs
