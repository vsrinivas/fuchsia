// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/snapshot.h>

#include <zxtest/zxtest.h>

namespace {

using inspect::Snapshot;
using inspect::internal::Block;
using inspect::internal::BlockType;
using inspect::internal::FreeBlockFields;
using inspect::internal::GetBlock;
using inspect::internal::HeaderBlockFields;
using inspect::internal::kMagicNumber;
using inspect::internal::kMinOrderSize;

TEST(Snapshot, ValidRead) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  memset(vmo.start(), 'a', 4096);
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);

  ASSERT_OK(status);
  ASSERT_EQ(4096u, snapshot.size());

  // Make sure that the data was actually fully copied to the snapshot.
  std::vector<uint8_t> buf;
  buf.resize(4096u - sizeof(Block));
  memset(buf.data(), 'a', buf.size());
  EXPECT_EQ(0, memcmp(snapshot.data() + sizeof(Block), buf.data(), buf.size()));
}

TEST(Snapshot, InvalidBufferSize) {
  for (size_t i = 0; i < kMinOrderSize; i++) {
    Snapshot snapshot;
    std::vector<uint8_t> buffer;
    buffer.resize(i);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, Snapshot::Create(std::move(buffer), &snapshot));
  }
}

TEST(Snapshot, GetBlock) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  memset(vmo.start(), 'a', 4096);
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  {
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);
    EXPECT_OK(status);
    // Can get block 0.
    EXPECT_NE(nullptr, GetBlock(&snapshot, 0));
    // Cannot get block past the end of the snapshot.
    EXPECT_EQ(nullptr, GetBlock(&snapshot, 4096 / kMinOrderSize));
  }

  Block* tester =
      reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(vmo.start()) + 4096 - kMinOrderSize * 2);
  size_t tester_index = 4096 / kMinOrderSize - 2;

  {
    tester->header =
        FreeBlockFields::Order::Make(1) | FreeBlockFields::Type::Make(BlockType::kFree);

    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);
    EXPECT_OK(status);
    // Can get tester, since it's adjacent to the end of the snapshot.
    EXPECT_NE(nullptr, GetBlock(&snapshot, tester_index));
  }
  {
    // Set the order to be too large for the buffer
    tester->header =
        FreeBlockFields::Order::Make(2) | FreeBlockFields::Type::Make(BlockType::kFree);

    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);
    EXPECT_OK(status);
    // Cannot get block, since its order is too large for the remaining space.
    EXPECT_EQ(nullptr, GetBlock(&snapshot, tester_index));
  }
}

TEST(Snapshot, InvalidWritePending) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 1;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
  EXPECT_FALSE(!!snapshot);
}

TEST(Snapshot, ValidPendingSkipCheck) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 1;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), {.read_attempts = 100, .skip_consistency_check = true}, &snapshot);
  EXPECT_OK(status);
  EXPECT_EQ(4096u, snapshot.size());
}

TEST(Snapshot, InvalidGenerationChange) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), Snapshot::kDefaultOptions,
      [header](uint8_t* buffer, size_t buffer_size) { header->payload.u64 += 2; }, &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
}

TEST(Snapshot, InvalidGenerationChangeFinalStep) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  int times_called = 0;
  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), Snapshot::Options{.read_attempts = 1, .skip_consistency_check = false},
      [&](uint8_t* buffer, size_t buffer_size) {
        // Only change the generation count after the second read obtaining the whole buffer.
        if (++times_called == 2) {
          header->payload.u64 += 2;
        }
      },
      &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
}

TEST(Snapshot, ValidGenerationChangeSkipCheck) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), {.read_attempts = 100, .skip_consistency_check = true},
      [header](uint8_t* buffer, size_t buffer_size) { header->payload.u64 += 2; }, &snapshot);

  EXPECT_OK(status);
  EXPECT_EQ(4096u, snapshot.size());
}

TEST(Snapshot, InvalidBadMagicNumber) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
}

TEST(Snapshot, InvalidBadMagicNumberSkipCheck) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), {.read_attempts = 100, .skip_consistency_check = true}, &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
}

}  // namespace
