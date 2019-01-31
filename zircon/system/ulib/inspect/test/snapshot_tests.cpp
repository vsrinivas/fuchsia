// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/block.h>
#include <lib/inspect/snapshot.h>
#include <unittest/unittest.h>

namespace {

using inspect::BlockType;
using inspect::Snapshot;
using inspect::internal::Block;
using inspect::internal::HeaderBlockFields;

bool ValidRead() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    memset(vmo.start(), 'a', 4096);
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
    header->payload.u64 = 0;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(std::move(dup), &snapshot);

    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(4096, snapshot.size());

    // Make sure that the data was actually fully copied to the snapshot.
    uint8_t buf[snapshot.size() - sizeof(Block)];
    memset(buf, 'a', snapshot.size() - sizeof(Block));
    EXPECT_EQ(0, memcmp(snapshot.data() + sizeof(Block), buf, snapshot.size() - sizeof(Block)));

    END_TEST;
}

bool InvalidWritePending() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
    header->payload.u64 = 1;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(std::move(dup), &snapshot);

    EXPECT_EQ(ZX_ERR_INTERNAL, status);
    EXPECT_FALSE(snapshot);

    END_TEST;
}

bool ValidPendingSkipCheck() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
    header->payload.u64 = 1;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(
        std::move(dup), {.read_attempts = 100, .skip_consistency_check = true}, &snapshot);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(4096, snapshot.size());

    END_TEST;
}

bool InvalidGenerationChange() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
    header->payload.u64 = 0;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(
        std::move(dup), Snapshot::kDefaultOptions,
        [header] (uint8_t* buffer, size_t buffer_size) { header->payload.u64 += 2; }, &snapshot);

    EXPECT_EQ(ZX_ERR_INTERNAL, status);

    END_TEST;
}

bool ValidGenerationChangeSkipCheck() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    memcpy(&header->header_data[4], inspect::kMagicNumber, 4);
    header->payload.u64 = 0;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(
        std::move(dup), {.read_attempts = 100, .skip_consistency_check = true},
        [header] (uint8_t* buffer, size_t buffer_size) { header->payload.u64 += 2; }, &snapshot);

    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(4096, snapshot.size());

    END_TEST;
}

bool InvalidBadMagicNumber() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    header->payload.u64 = 0;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(std::move(dup), &snapshot);

    EXPECT_EQ(ZX_ERR_INTERNAL, status);

    END_TEST;
}

bool InvalidBadMagicNumberSkipCheck() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(ZX_OK, vmo.CreateAndMap(4096, "test"));
    Block* header = reinterpret_cast<Block*>(vmo.start());
    header->header = HeaderBlockFields::Order::Make(0) |
                     HeaderBlockFields::Type::Make(BlockType::kHeader) |
                     HeaderBlockFields::Version::Make(0);
    header->payload.u64 = 0;

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, vmo.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    Snapshot snapshot;
    zx_status_t status = Snapshot::Create(
        std::move(dup), {.read_attempts = 100, .skip_consistency_check = true}, &snapshot);

    EXPECT_EQ(ZX_ERR_INTERNAL, status);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(SnapshotTests)
RUN_TEST(ValidRead)
RUN_TEST(InvalidWritePending)
RUN_TEST(ValidPendingSkipCheck)
RUN_TEST(InvalidGenerationChange)
RUN_TEST(ValidGenerationChangeSkipCheck)
RUN_TEST(InvalidBadMagicNumber)
RUN_TEST(InvalidBadMagicNumberSkipCheck)
END_TEST_CASE(SnapshotTests)
