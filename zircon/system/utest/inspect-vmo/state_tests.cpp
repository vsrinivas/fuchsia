// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/inspect-vmo/block.h>
#include <lib/inspect-vmo/scanner.h>
#include <lib/inspect-vmo/snapshot.h>
#include <lib/inspect-vmo/state.h>
#include <unittest/unittest.h>

namespace {

using inspect::vmo::ArrayFormat;
using inspect::vmo::BlockType;
using inspect::vmo::DoubleArray;
using inspect::vmo::DoubleMetric;
using inspect::vmo::IntArray;
using inspect::vmo::IntMetric;
using inspect::vmo::kNumOrders;
using inspect::vmo::Object;
using inspect::vmo::Property;
using inspect::vmo::PropertyFormat;
using inspect::vmo::Snapshot;
using inspect::vmo::UintArray;
using inspect::vmo::UintMetric;
using inspect::vmo::internal::ArrayBlockPayload;
using inspect::vmo::internal::Block;
using inspect::vmo::internal::BlockIndex;
using inspect::vmo::internal::ExtentBlockFields;
using inspect::vmo::internal::HeaderBlockFields;
using inspect::vmo::internal::Heap;
using inspect::vmo::internal::NameBlockFields;
using inspect::vmo::internal::PropertyBlockPayload;
using inspect::vmo::internal::State;
using inspect::vmo::internal::ValueBlockFields;

// Container for scanned blocks from the buffer.
// TODO(CF-236): Use std::map instead of intrusive containers when
// libstd++ is available.
struct ScannedBlock : public fbl::WAVLTreeContainable<fbl::unique_ptr<ScannedBlock>> {
    BlockIndex index;
    const Block* block;

    ScannedBlock(BlockIndex index, const Block* block)
        : index(index), block(block) {}

    BlockIndex GetKey() const { return index; }
};

bool CompareBlock(const Block* actual, const Block expected) {
    BEGIN_HELPER;

    EXPECT_BYTES_EQ((const uint8_t*)(&expected), (const uint8_t*)(actual), sizeof(Block),
                    "Block header contents did not match");

    END_HELPER;
}

template <typename T>
bool CompareArray(const Block* block, const T* expected, size_t count) {
    BEGIN_HELPER;
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(&block->payload) + 8,
                    reinterpret_cast<const uint8_t*>(expected),
                    sizeof(int64_t) * count,
                    "Array payload does not match");

    END_HELPER;
}

Block MakeBlock(uint64_t header) {
    Block ret;
    ret.header = header;
    ret.payload.u64 = 0;
    return ret;
}

Block MakeBlock(uint64_t header, const char payload[9]) {
    Block ret;
    ret.header = header;
    memcpy(ret.payload.data, payload, 8);
    return ret;
}

Block MakeBlock(uint64_t header, uint64_t payload) {
    Block ret;
    ret.header = header;
    ret.payload.u64 = payload;
    return ret;
}

Block MakeIntBlock(uint64_t header, int64_t payload) {
    Block ret;
    ret.header = header;
    ret.payload.i64 = payload;
    return ret;
}

Block MakeDoubleBlock(uint64_t header, double payload) {
    Block ret;
    ret.header = header;
    ret.payload.f64 = payload;
    return ret;
}

Block MakeHeader(uint64_t generation) {
    Block ret;
    ret.header = HeaderBlockFields::Type::Make(BlockType::kHeader) |
                 HeaderBlockFields::Order::Make(0) | HeaderBlockFields::Version::Make(0);
    memcpy(&ret.header_data[4], inspect::vmo::kMagicNumber, 4);
    ret.payload.u64 = generation;
    return ret;
}

Snapshot SnapshotAndScan(const zx::vmo& vmo,
                         fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>>* blocks,
                         size_t* free_blocks, size_t* allocated_blocks) {
    *free_blocks = *allocated_blocks = 0;

    Snapshot snapshot;
    Snapshot::Create(vmo, &snapshot);
    if (snapshot) {
        inspect::vmo::internal::ScanBlocks(
            snapshot.data(), snapshot.size(), [&](BlockIndex index, const Block* block) {
                if (inspect::vmo::internal::GetType(block) == BlockType::kFree) {
                    *free_blocks += 1;
                } else {
                    *allocated_blocks += 1;
                }
                blocks->insert(std::make_unique<ScannedBlock>(index, block));
            });
    }
    return snapshot;
}

bool CreateIntMetric() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    IntMetric a = state->CreateIntMetric("a", 0, 0);
    IntMetric b = state->CreateIntMetric("b", 0, 0);
    IntMetric c = state->CreateIntMetric("c", 0, 0);

    a.Set(10);
    b.Add(5);
    b.Subtract(10);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header and 2 for each metric.
    EXPECT_EQ(7, allocated_blocks);
    EXPECT_EQ(6, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(12)));
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                              ValueBlockFields::NameIndex::Make(2),
                                          10)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                              ValueBlockFields::NameIndex::Make(4),
                                          -5)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "b\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(5)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                              ValueBlockFields::NameIndex::Make(6),
                                          0)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(6)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "c\0\0\0\0\0\0\0")));

    END_TEST;
}

bool CreateUintMetric() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    UintMetric a = state->CreateUintMetric("a", 0, 0);
    UintMetric b = state->CreateUintMetric("b", 0, 0);
    UintMetric c = state->CreateUintMetric("c", 0, 0);

    a.Set(10);
    b.Add(15);
    b.Subtract(10);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header and 2 for each metric.
    EXPECT_EQ(7, allocated_blocks);
    EXPECT_EQ(6, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(12)));
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       10)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                           ValueBlockFields::NameIndex::Make(4),
                                       5)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "b\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(5)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                              ValueBlockFields::NameIndex::Make(6),
                                          0)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(6)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "c\0\0\0\0\0\0\0")));

    END_TEST;
}

bool CreateDoubleMetric() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    DoubleMetric a = state->CreateDoubleMetric("a", 0, 0);
    DoubleMetric b = state->CreateDoubleMetric("b", 0, 0);
    DoubleMetric c = state->CreateDoubleMetric("c", 0, 0);

    a.Set(3.25);
    b.Add(0.5);
    b.Subtract(0.25);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header and 2 for each metric.
    EXPECT_EQ(7, allocated_blocks);
    EXPECT_EQ(6, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(12)));
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeDoubleBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                                 ValueBlockFields::NameIndex::Make(2),
                                             3.25)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeDoubleBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                                 ValueBlockFields::NameIndex::Make(4),
                                             0.25)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "b\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(5)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                              ValueBlockFields::NameIndex::Make(6),
                                          0)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(6)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "c\0\0\0\0\0\0\0")));

    END_TEST;
}

bool CreateArrays() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    IntArray a = state->CreateIntArray("a", 0, 10, ArrayFormat::kLinearHistogram);
    UintArray b = state->CreateUintArray("b", 0, 10, ArrayFormat::kDefault);
    DoubleArray c = state->CreateDoubleArray("c", 0, 10, ArrayFormat::kDefault);

    a.Add(0, 10);
    a.Set(1, -10);
    a.Subtract(2, 9);
    // out of bounds
    a.Set(10, -10);
    a.Add(10, 0xFF);
    a.Subtract(10, 0xDD);

    b.Add(0, 10);
    b.Set(1, 10);
    b.Subtract(1, 9);
    // out of bounds
    b.Set(10, 10);
    b.Add(10, 10);
    b.Subtract(10, 10);

    c.Add(0, .25);
    c.Set(1, 1.25);
    c.Subtract(1, .5);
    // out of bounds
    c.Set(10, 10);
    c.Add(10, 10);
    c.Subtract(10, 10);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header and 2 for each metric.
    EXPECT_EQ(7, allocated_blocks);
    EXPECT_EQ(4, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(42)));

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(1)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "a\0\0\0\0\0\0\0")));
        EXPECT_TRUE(CompareBlock(blocks.find(8)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(1),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kLinearHistogram) |
                                               ArrayBlockPayload::Count::Make(10))));
        int64_t a_array_values[] = {10, -10, -9, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(8)->block, a_array_values, 10));
    }

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(2)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "b\0\0\0\0\0\0\0")));

        EXPECT_TRUE(CompareBlock(blocks.find(16)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(2),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kDefault) |
                                               ArrayBlockPayload::Count::Make(10))));
        uint64_t b_array_values[] = {10, 1, 0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(16)->block, b_array_values, 10));
    }

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(3)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "c\0\0\0\0\0\0\0")));

        EXPECT_TRUE(CompareBlock(blocks.find(24)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(3),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kDefault) |
                                               ArrayBlockPayload::Count::Make(10))));
        double c_array_values[] = {.25, .75, 0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(24)->block, c_array_values, 10));
    }

    END_TEST;
}

bool CreateArrayChildren() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    Object root = state->CreateObject("root", 0);

    IntArray a = root.CreateIntArray("a", 10, ArrayFormat::kLinearHistogram);
    UintArray b = root.CreateUintArray("b", 10, ArrayFormat::kDefault);
    DoubleArray c = root.CreateDoubleArray("c", 10, ArrayFormat::kDefault);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header and 2 for each metric.
    EXPECT_EQ(9, allocated_blocks);
    EXPECT_EQ(4, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(8)));

    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                           ValueBlockFields::ParentIndex::Make(0) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       3)));

    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(4),
                  "root\0\0\0\0")));

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(3)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "a\0\0\0\0\0\0\0")));
        EXPECT_TRUE(CompareBlock(blocks.find(8)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::ParentIndex::Make(1) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(3),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kLinearHistogram) |
                                               ArrayBlockPayload::Count::Make(10))));
        int64_t a_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(8)->block, a_array_values, 10));
    }

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(4)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "b\0\0\0\0\0\0\0")));

        EXPECT_TRUE(CompareBlock(blocks.find(16)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::ParentIndex::Make(1) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(4),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kDefault) |
                                               ArrayBlockPayload::Count::Make(10))));
        uint64_t b_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(16)->block, b_array_values, 10));
    }

    {
        EXPECT_TRUE(CompareBlock(
            blocks.find(5)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "c\0\0\0\0\0\0\0")));

        EXPECT_TRUE(CompareBlock(blocks.find(24)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                                               ValueBlockFields::ParentIndex::Make(1) |
                                               ValueBlockFields::Order::Make(3) |
                                               ValueBlockFields::NameIndex::Make(5),
                                           ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                                               ArrayBlockPayload::Flags::Make(ArrayFormat::kDefault) |
                                               ArrayBlockPayload::Count::Make(10))));
        double c_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_TRUE(CompareArray(blocks.find(24)->block, c_array_values, 10));
    }

    END_TEST;
}

bool CreateSmallProperties() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    Property a = state->CreateProperty("a", 0, "Hello", PropertyFormat::kUtf8);
    Property b = state->CreateProperty("b", 0, "88888888", PropertyFormat::kBinary);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), 2 single extent properties (6)
    EXPECT_EQ(1 + 6, allocated_blocks);
    EXPECT_EQ(6, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(4)));

    // Property a fits in the first 3 blocks (value, name, extent).
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       PropertyBlockPayload::ExtentIndex::Make(3) |
                                           PropertyBlockPayload::TotalLength::Make(5))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(
        CompareBlock(blocks.find(3)->block,
                     MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "Hello\0\0\0")));

    // Property b fits in the next 3 blocks (value, name, extent).
    EXPECT_TRUE(CompareBlock(blocks.find(4)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(5),
                                       PropertyBlockPayload::ExtentIndex::Make(6) |
                                           PropertyBlockPayload::TotalLength::Make(8) |
                                           PropertyBlockPayload::Flags::Make(PropertyFormat::kBinary))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(5)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "b\0\0\0\0\0\0\0")));
    EXPECT_TRUE(
        CompareBlock(blocks.find(6)->block,
                     MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "88888888")));

    END_TEST;
}

bool CreateLargeSingleExtentProperties() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    char input[] = "abcdefg";
    size_t input_size = 7;
    char contents[2041];
    for (int i = 0; i < 2040; i++) {
        contents[i] = input[i % input_size];
    }
    contents[2040] = 0;
    Property a = state->CreateProperty("a", 0, {contents, 2040}, PropertyFormat::kUtf8);
    Property b = state->CreateProperty("b", 0, {contents, 2040}, PropertyFormat::kBinary);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), 2 single extent properties (6)
    EXPECT_EQ(1 + 6, allocated_blocks);
    EXPECT_EQ(7, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(4)));

    // Property a has the first 2 blocks for value and name, but needs a large block for the
    // contents.
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       PropertyBlockPayload::ExtentIndex::Make(128) |
                                           PropertyBlockPayload::TotalLength::Make(2040))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(128)->block,
                             MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                                           ExtentBlockFields::Order::Make(kNumOrders - 1),
                                       "abcdefga")));
    EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents, 2040));

    // Property b has the next 2 blocks at the beginning for its value and name, but it claims
    // another large block for the extent.
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(4),
                                       PropertyBlockPayload::ExtentIndex::Make(256) |
                                           PropertyBlockPayload::TotalLength::Make(2040) |
                                           PropertyBlockPayload::Flags::Make(PropertyFormat::kBinary))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "b\0\0\0\0\0\0\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(256)->block,
                             MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                                           ExtentBlockFields::Order::Make(kNumOrders - 1),
                                       "abcdefga")));
    EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents, 2040));

    END_TEST;
}

bool CreateMultiExtentProperty() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    char input[] = "abcdefg";
    size_t input_size = 7;
    char contents[6001];
    for (int i = 0; i < 6000; i++) {
        contents[i] = input[i % input_size];
    }
    contents[6000] = 0;
    Property a = state->CreateProperty("a", 0, {contents, 6000}, PropertyFormat::kUtf8);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), 1 property (2) with 3 extents (3)
    EXPECT_EQ(1 + 2 + 3, allocated_blocks);
    EXPECT_EQ(6, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(2)));

    // Property a has the first 2 blocks for its value and name.
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       PropertyBlockPayload::ExtentIndex::Make(128) |
                                           PropertyBlockPayload::TotalLength::Make(6000))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    // Extents are threaded between blocks 128, 256, and 384.
    EXPECT_TRUE(CompareBlock(blocks.find(128)->block,
                             MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                                           ExtentBlockFields::Order::Make(kNumOrders - 1) |
                                           ExtentBlockFields::NextExtentIndex::Make(256),
                                       "abcdefga")));
    EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents, 2040));
    EXPECT_TRUE(CompareBlock(blocks.find(256)->block,
                             MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                                           ExtentBlockFields::Order::Make(kNumOrders - 1) |
                                           ExtentBlockFields::NextExtentIndex::Make(384),
                                       "defgabcd")));
    EXPECT_EQ(0, memcmp(blocks.find(256)->block->payload.data, contents + 2040, 2040));
    EXPECT_TRUE(CompareBlock(blocks.find(384)->block,
                             MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                                           ExtentBlockFields::Order::Make(kNumOrders - 1),
                                       "gabcdefg")));
    EXPECT_EQ(0,
              memcmp(blocks.find(384)->block->payload.data, contents + 2 * 2040, 6000 - 2 * 2040));

    END_TEST;
}

bool SetSmallProperty() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    struct test_case {
        int expected_generation;
        PropertyFormat format;
    };
    fbl::Vector<test_case> cases = {{4, PropertyFormat::kUtf8}, {10, PropertyFormat::kBinary}};

    for (const auto& test : cases) {
        Property a = state->CreateProperty("a", 0, "Hello", test.format);

        a.Set("World");

        fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
        size_t free_blocks, allocated_blocks;
        auto snapshot =
            SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
        ASSERT_TRUE(snapshot);

        // Header (1), 1 single extent property (3)
        EXPECT_EQ(1 + 3, allocated_blocks);
        EXPECT_EQ(6, free_blocks);

        EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(test.expected_generation)));

        // Property a fits in the first 3 blocks (value, name, extent).
        EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                                 MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                               ValueBlockFields::NameIndex::Make(2),
                                           PropertyBlockPayload::ExtentIndex::Make(3) |
                                               PropertyBlockPayload::TotalLength::Make(5) |
                                               PropertyBlockPayload::Flags::Make(test.format))));
        EXPECT_TRUE(CompareBlock(
            blocks.find(2)->block,
            MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                      "a\0\0\0\0\0\0\0")));
        EXPECT_TRUE(
            CompareBlock(blocks.find(3)->block,
                         MakeBlock(ValueBlockFields::Type::Make(BlockType::kExtent), "World\0\0\0")));
    }

    END_TEST;
}

bool SetLargeProperty() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    char input[] = "abcdefg";
    size_t input_size = 7;
    char contents[6001];
    for (int i = 0; i < 6000; i++) {
        contents[i] = input[i % input_size];
    }
    contents[6000] = '\0';

    Property a = state->CreateProperty("a", 0, {contents, 6000}, PropertyFormat::kUtf8);

    a.Set("World");

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), 1 single extent property (3)
    EXPECT_EQ(1 + 3, allocated_blocks);
    EXPECT_EQ(8, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(4)));

    // Property a fits in the first 3 blocks (value, name, extent).
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       PropertyBlockPayload::ExtentIndex::Make(3) |
                                           PropertyBlockPayload::TotalLength::Make(5))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));
    EXPECT_TRUE(
        CompareBlock(blocks.find(3)->block,
                     MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "World\0\0\0")));

    END_TEST;
}

bool SetPropertyOutOfMemory() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo), 16 * 1024);
    auto state = State::Create(std::move(heap));

    fbl::Vector<char> vec;
    for (int i = 0; i < 65000; i++) {
        vec.push_back('a');
    }

    Property a = state->CreateProperty("a", 0, {vec.begin(), vec.size()}, PropertyFormat::kUtf8);
    EXPECT_FALSE(bool(a));

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1) only, property failed to fit.
    EXPECT_EQ(1, allocated_blocks);
    EXPECT_EQ(14, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(2)));
    END_TEST;
}

bool CreateObjectHierarchy() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    Object root = state->CreateObject("objects", 0);
    auto req = root.CreateChild("requests");
    auto network = req.CreateUintMetric("network", 10);
    auto wifi = req.CreateUintMetric("wifi", 5);

    auto version = root.CreateProperty("version", "1.0beta2", PropertyFormat::kUtf8);

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), root (2), requests (2), 2 metrics (4), small property (3)
    EXPECT_EQ(1 + 2 + 2 + 4 + 3, allocated_blocks);
    EXPECT_EQ(5, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(10)));

    // Root object is at index 1.
    // It has 2 references (req and version).
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                           ValueBlockFields::ParentIndex::Make(0) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       2)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(7),
                  "objects\0")));

    // Requests object is at index 3.
    // It has 2 references (wifi and network).
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                           ValueBlockFields::ParentIndex::Make(1) |
                                           ValueBlockFields::NameIndex::Make(4),
                                       2)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(8),
                  "requests")));

    // Network value
    EXPECT_TRUE(CompareBlock(blocks.find(5)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                           ValueBlockFields::ParentIndex::Make(3) |
                                           ValueBlockFields::NameIndex::Make(6),
                                       10)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(6)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(7),
                  "network\0")));

    // Wifi value
    EXPECT_TRUE(CompareBlock(blocks.find(7)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                           ValueBlockFields::ParentIndex::Make(3) |
                                           ValueBlockFields::NameIndex::Make(8),
                                       5)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(8)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(4),
                  "wifi\0\0\0\0")));

    // Version property
    EXPECT_TRUE(CompareBlock(blocks.find(9)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                                           ValueBlockFields::ParentIndex::Make(1) |
                                           ValueBlockFields::NameIndex::Make(10),
                                       PropertyBlockPayload::ExtentIndex::Make(11) |
                                           PropertyBlockPayload::TotalLength::Make(8))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(10)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(7),
                  "version\0")));
    EXPECT_TRUE(
        CompareBlock(blocks.find(11)->block,
                     MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "1.0beta2")));

    END_TEST;
}

bool TombstoneTest() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    fbl::unique_ptr<Object> requests;
    {
        // Root going out of scope causes a tombstone to be created,
        // but since requests is referencing it it will not be deleted.
        Object root = state->CreateObject("objects", 0);
        requests = std::make_unique<Object>(root.CreateChild("requests"));
        auto a = root.CreateIntMetric("a", 1);
        auto b = root.CreateUintMetric("b", 1);
        auto c = root.CreateDoubleMetric("c", 1);
    }

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // Header (1), root tombstone (2), requests (2)
    EXPECT_EQ(1 + 2 + 2, allocated_blocks);
    EXPECT_EQ(7, free_blocks);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(18)));

    // Root object is at index 1, but has been tombstoned.
    // It has 1 reference (requests)
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kTombstone) |
                                           ValueBlockFields::ParentIndex::Make(0) |
                                           ValueBlockFields::NameIndex::Make(2),
                                       1)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(7),
                  "objects\0")));
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                       ValueBlockFields::ParentIndex::Make(1) |
                                       ValueBlockFields::NameIndex::Make(4))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(8),
                  "requests")));

    END_TEST;
}

bool TombstoneCleanup() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    IntMetric metric = state->CreateIntMetric("a", 0, 0);

    Object root = state->CreateObject("root", 0);
    {
        Object child1 = state->CreateObject("child1", 0);
        Object child2 = child1.CreateChild("child2");

        {
            Object child = child1.CreateChild("this_is_a_child");
            std::unique_ptr<IntMetric> m;
            {
                Object new_child = root.CreateChild("child");
                m = std::make_unique<IntMetric>(new_child.CreateIntMetric("value", -1));
            }
            Property temp = child.CreateProperty("temp", "test", PropertyFormat::kUtf8);
            m.reset();
        }
    }

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    // 2 each for:
    // metric create
    // root create
    // child1 create
    // child2 create
    // child create
    // new_child
    // m create
    // new_child delete (tombstone)
    // temp create
    // m delete
    // temp delete
    // child delete
    // child2 delete
    // child1 delete
    EXPECT_TRUE(CompareBlock(blocks.find(0)->block, MakeHeader(14 * 2)));

    // Metric "a" is at index 1.
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                              ValueBlockFields::ParentIndex::Make(0) |
                                              ValueBlockFields::NameIndex::Make(2),
                                          0)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));

    // Root object is at index 3.
    // It has 0 references since the children should be removed.
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                       ValueBlockFields::ParentIndex::Make(0) |
                                       ValueBlockFields::NameIndex::Make(4))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(4),
                  "root\0\0\0\0")));

    END_TEST;
}

constexpr size_t kThreadTimes = 1024 * 10;

struct ThreadArgs {
    IntMetric* metric;
    uint64_t value;
    bool add;
};

int ValueThread(void* input) {
    auto* args = reinterpret_cast<ThreadArgs*>(input);
    for (size_t i = 0; i < kThreadTimes; i++) {
        if (args->add) {
            args->metric->Add(args->value);
        } else {
            args->metric->Subtract(args->value);
        }
    }

    return 0;
}

int ChildThread(void* input) {
    Object* object = reinterpret_cast<Object*>(input);
    for (size_t i = 0; i < kThreadTimes; i++) {
        Object child = object->CreateChild("this_is_a_child");
        Property temp = child.CreateProperty("temp", "test", PropertyFormat::kUtf8);
    }
    return 0;
}

bool MultithreadingTest() {
    BEGIN_TEST;

    auto vmo = fzl::ResizeableVmoMapper::Create(4096, "test");
    ASSERT_TRUE(vmo != nullptr);
    auto heap = std::make_unique<Heap>(std::move(vmo));
    auto state = State::Create(std::move(heap));

    size_t per_thread_times_operation_count = 0;
    size_t other_operation_count = 0;

    other_operation_count += 1; // create a
    IntMetric metric = state->CreateIntMetric("a", 0, 0);

    ThreadArgs adder{.metric = &metric, .value = 2, .add = true};
    ThreadArgs subtractor{.metric = &metric, .value = 1, .add = false};

    thrd_t add_thread, subtract_thread, child_thread_1, child_thread_2;

    other_operation_count += 1; // create root
    Object root = state->CreateObject("root", 0);
    {
        other_operation_count += 2; // create and delete
        Object child1 = state->CreateObject("child1", 0);
        other_operation_count += 2; // create and delete
        Object child2 = child1.CreateChild("child2");

        per_thread_times_operation_count += 1; // add metric
        thrd_create(&add_thread, ValueThread, &adder);

        per_thread_times_operation_count += 1; // subtract metric
        thrd_create(&subtract_thread, ValueThread, &subtractor);

        per_thread_times_operation_count += 4; // create child, create temp, delete both
        thrd_create(&child_thread_1, ChildThread, &child1);
        per_thread_times_operation_count += 4; // create child, create temp, delete both
        thrd_create(&child_thread_2, ChildThread, &child2);

        per_thread_times_operation_count += 4; // create child, create m, delete both;
        for (size_t i = 0; i < kThreadTimes; i++) {
            Object child = root.CreateChild("child");
            IntMetric m = child.CreateIntMetric("value", -1);
        }
        thrd_join(add_thread, nullptr);
        thrd_join(subtract_thread, nullptr);
        thrd_join(child_thread_1, nullptr);
        thrd_join(child_thread_2, nullptr);
    }

    fbl::WAVLTree<BlockIndex, fbl::unique_ptr<ScannedBlock>> blocks;
    size_t free_blocks, allocated_blocks;
    auto snapshot =
        SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
    ASSERT_TRUE(snapshot);

    EXPECT_TRUE(CompareBlock(blocks.find(0)->block,
                             MakeHeader(kThreadTimes * per_thread_times_operation_count * 2 +
                                        other_operation_count * 2)));

    // Metric "a" is at index 1.
    // Its value should be equal to kThreadTimes since subtraction
    // should cancel out half of addition.
    EXPECT_TRUE(CompareBlock(blocks.find(1)->block,
                             MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                              ValueBlockFields::ParentIndex::Make(0) |
                                              ValueBlockFields::NameIndex::Make(2),
                                          kThreadTimes)));
    EXPECT_TRUE(CompareBlock(
        blocks.find(2)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(1),
                  "a\0\0\0\0\0\0\0")));

    // Root object is at index 3.
    // It has 0 references since the children should be removed.
    EXPECT_TRUE(CompareBlock(blocks.find(3)->block,
                             MakeBlock(ValueBlockFields::Type::Make(BlockType::kObjectValue) |
                                       ValueBlockFields::ParentIndex::Make(0) |
                                       ValueBlockFields::NameIndex::Make(4))));
    EXPECT_TRUE(CompareBlock(
        blocks.find(4)->block,
        MakeBlock(NameBlockFields::Type::Make(BlockType::kName) | NameBlockFields::Length::Make(4),
                  "root\0\0\0\0")));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(StateTests)
RUN_TEST(CreateIntMetric)
RUN_TEST(CreateUintMetric)
RUN_TEST(CreateDoubleMetric)
RUN_TEST(CreateArrays)
RUN_TEST(CreateArrayChildren)
RUN_TEST(CreateSmallProperties)
RUN_TEST(CreateLargeSingleExtentProperties)
RUN_TEST(CreateMultiExtentProperty)
RUN_TEST(SetSmallProperty)
RUN_TEST(SetLargeProperty)
RUN_TEST(SetPropertyOutOfMemory)
RUN_TEST(CreateObjectHierarchy)
RUN_TEST(TombstoneTest)
RUN_TEST(TombstoneCleanup)
RUN_TEST(MultithreadingTest)
END_TEST_CASE(StateTests)
