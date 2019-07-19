// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/snapshot.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::Block;
using inspect::BlockType;
using inspect::HeaderBlockFields;
using inspect::Snapshot;

TEST(Snapshot, ValidRead) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  memset(vmo.start(), 'a', 4096);
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);

  EXPECT_OK(status);
  EXPECT_EQ(4096u, snapshot.size());

  // Make sure that the data was actually fully copied to the snapshot.
  uint8_t buf[snapshot.size() - sizeof(Block)];
  memset(buf, 'a', snapshot.size() - sizeof(Block));
  EXPECT_EQ(0, memcmp(snapshot.data() + sizeof(Block), buf, snapshot.size() - sizeof(Block)));
}

TEST(Snapshot, InvalidWritePending) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
  header->payload.u64 = 1;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(vmo.vmo(), &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
  EXPECT_FALSE(snapshot);
}

TEST(Snapshot, ValidPendingSkipCheck) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
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
  memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
  header->payload.u64 = 0;

  Snapshot snapshot;
  zx_status_t status = Snapshot::Create(
      vmo.vmo(), Snapshot::kDefaultOptions,
      [header](uint8_t* buffer, size_t buffer_size) { header->payload.u64 += 2; }, &snapshot);

  EXPECT_EQ(ZX_ERR_INTERNAL, status);
}

TEST(Snapshot, ValidGenerationChangeSkipCheck) {
  fzl::OwnedVmoMapper vmo;
  ASSERT_OK(vmo.CreateAndMap(4096, "test"));
  Block* header = reinterpret_cast<Block*>(vmo.start());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
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
