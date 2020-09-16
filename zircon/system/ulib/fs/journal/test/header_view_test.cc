// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/journal/format.h>
#include <fs/journal/header_view.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

constexpr size_t kBlockSize = kJournalBlockSize;
constexpr uint64_t kPayloadBlocks = 10;
constexpr uint64_t kSequenceNumber = 20;

TEST(JournalHeaderView, JournalHeaderView) {
  uint8_t block[kBlockSize] = {};
  JournalHeaderView header(fbl::Span<uint8_t>(block, kBlockSize));
}

TEST(JournalHeaderView, Initialize) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  ASSERT_EQ(kPayloadBlocks, header.PayloadBlocks());
  ASSERT_EQ(kSequenceNumber, header.SequenceNumber());

  for (uint32_t i = 0; i < kPayloadBlocks; i++) {
    ASSERT_EQ(header.TargetBlock(i), 0ul);
    ASSERT_FALSE(header.EscapedBlock(i));
  }
}

TEST(JournalHeaderView, LoadValidHeader) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber).value();
  ASSERT_EQ(loaded.PayloadBlocks(), header.PayloadBlocks());
  ASSERT_EQ(loaded.SequenceNumber(), header.SequenceNumber());
  ASSERT_EQ(loaded.ObjectType(), JournalObjectType::kHeader);
}

TEST(JournalHeaderView, LoadValidRevocation) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  auto prefix = reinterpret_cast<JournalPrefix*>(block);
  prefix->flags = kJournalPrefixFlagRevocation;

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber).value();
  ASSERT_EQ(loaded.PayloadBlocks(), header.PayloadBlocks());
  ASSERT_EQ(loaded.SequenceNumber(), header.SequenceNumber());
  ASSERT_EQ(loaded.ObjectType(), JournalObjectType::kRevocation);
}

TEST(JournalHeaderView, LoadBadMagicNumber) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber);
  ASSERT_EQ(ZX_ERR_BAD_STATE, loaded.error());
}

TEST(JournalHeaderView, LoadSmallBuffer) {
  uint8_t block[kBlockSize - 1] = {};
  fbl::Span<uint8_t> span(block, sizeof(block));

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber);
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, loaded.error());
}

TEST(JournalHeaderView, SetTargetBlock) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  for (uint32_t i = 0; i < kPayloadBlocks; i++) {
    header.SetTargetBlock(i, i + 1);
  }

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber).value();
  for (uint32_t i = 0; i < kPayloadBlocks; i++) {
    ASSERT_EQ(i + 1, header.TargetBlock(i));
    ASSERT_EQ(i + 1, loaded.TargetBlock(i));
  }
}

TEST(JournalHeaderView, TargetBlockPtr) {
  uint8_t block[kBlockSize] = {};
  auto header_block = reinterpret_cast<const JournalHeaderBlock*>(block);
  fbl::Span<uint8_t> span(block, kBlockSize);
  uint32_t target_block = 3;

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  header.SetTargetBlock(target_block, target_block);
  auto ptr = header.TargetBlockPtr(target_block);

  ASSERT_NE(ptr, nullptr);
  ASSERT_EQ(ptr, &header_block->target_blocks[target_block]);
  ASSERT_EQ(*ptr, header_block->target_blocks[target_block]);
}

TEST(JournalHeaderView, SetEscapedBlock) {
  uint8_t block[kBlockSize] = {};
  auto header_block = reinterpret_cast<const JournalHeaderBlock*>(block);
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  for (uint32_t i = 0; i < kPayloadBlocks; i++) {
    header.SetEscapedBlock(i, (i % 2) == 0);
  }

  auto loaded = fs::JournalHeaderView::Create(span, kSequenceNumber).value();
  for (uint32_t i = 0; i < kPayloadBlocks; i++) {
    ASSERT_EQ((i % 2) == 0, header.EscapedBlock(i));
    ASSERT_EQ((i % 2) == 0, loaded.EscapedBlock(i));
    ASSERT_EQ((i % 2) == 0,
              (header_block->target_flags[i] & kJournalBlockDescriptorFlagEscapedBlock) ==
                  kJournalBlockDescriptorFlagEscapedBlock);
  }
}

TEST(JournalHeaderView, PayloadBlocks) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);
  uint32_t payload_blocks = 5;

  JournalHeaderView header(span, payload_blocks, kSequenceNumber);
  ASSERT_EQ(payload_blocks, header.PayloadBlocks());
}

TEST(JournalHeaderView, PayloadBlocksPtr) {
  uint8_t block[kBlockSize] = {};
  auto header_block = reinterpret_cast<const JournalHeaderBlock*>(block);
  fbl::Span<uint8_t> span(block, kBlockSize);

  JournalHeaderView header(span, kPayloadBlocks, kSequenceNumber);
  auto ptr = header.PayloadBlocksPtr();

  ASSERT_NE(ptr, nullptr);
  ASSERT_EQ(ptr, &header_block->payload_blocks);
  ASSERT_EQ(*ptr, header_block->payload_blocks);
}

TEST(JournalHeaderView, SequenceNumber) {
  uint8_t block[kBlockSize] = {};
  fbl::Span<uint8_t> span(block, kBlockSize);
  uint32_t sequence_number = 33;

  JournalHeaderView header(span, kPayloadBlocks, sequence_number);
  ASSERT_EQ(sequence_number, header.SequenceNumber());
}

}  // namespace

}  // namespace fs
