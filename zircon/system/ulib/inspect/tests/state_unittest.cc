// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/4755): Use std::map instead of FBL in this file.
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/scanner.h>
#include <lib/inspect/cpp/vmo/snapshot.h>
#include <lib/inspect/cpp/vmo/state.h>
#include <threads.h>

#include <iostream>
#include <memory>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/vector.h>
#include <pretty/hexdump.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::BoolProperty;
using inspect::ByteVectorProperty;
using inspect::DoubleArray;
using inspect::DoubleProperty;
using inspect::IntArray;
using inspect::IntProperty;
using inspect::Link;
using inspect::Node;
using inspect::Snapshot;
using inspect::StringProperty;
using inspect::UintArray;
using inspect::UintProperty;
using inspect::internal::ArrayBlockFormat;
using inspect::internal::ArrayBlockPayload;
using inspect::internal::Block;
using inspect::internal::BlockIndex;
using inspect::internal::BlockType;
using inspect::internal::ExtentBlockFields;
using inspect::internal::HeaderBlockFields;
using inspect::internal::Heap;
using inspect::internal::kMagicNumber;
using inspect::internal::kNumOrders;
using inspect::internal::LinkBlockDisposition;
using inspect::internal::LinkBlockPayload;
using inspect::internal::NameBlockFields;
using inspect::internal::PropertyBlockFormat;
using inspect::internal::PropertyBlockPayload;
using inspect::internal::ScanBlocks;
using inspect::internal::State;
using inspect::internal::ValueBlockFields;

std::shared_ptr<State> InitState(size_t size) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(size, 0, &vmo));
  if (!bool(vmo)) {
    return NULL;
  }
  auto heap = std::make_unique<Heap>(std::move(vmo));
  return State::Create(std::move(heap));
}

// Container for scanned blocks from the buffer.
// TODO(fxbug.dev/4130): Use std::map instead of intrusive containers when
// libstd++ is available.
struct ScannedBlock : public fbl::WAVLTreeContainable<std::unique_ptr<ScannedBlock>> {
  BlockIndex index;
  const Block* block;

  ScannedBlock(BlockIndex index, const Block* block) : index(index), block(block) {}

  BlockIndex GetKey() const { return index; }
};

// Helper to just print blocks, return empty string to allow triggering as part of context for an
// assertion.
std::string print_block(const Block* block) {
  hexdump8(block, sizeof(Block));
  return "";
}

void CompareBlock(const Block* actual, const Block expected) {
  if (memcmp((const uint8_t*)(&expected), (const uint8_t*)(actual), sizeof(Block)) != 0) {
    std::cout << "Block header contents did not match" << print_block(&expected)
              << print_block(actual);
    EXPECT_TRUE(false);
  }
}

template <typename T>
void CompareArray(const Block* block, const T* expected, size_t count) {
  EXPECT_EQ(0,
            memcmp(reinterpret_cast<const uint8_t*>(expected),
                   reinterpret_cast<const uint8_t*>(&block->payload) + 8, sizeof(int64_t) * count));
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

Block MakeBoolBlock(uint64_t header, bool payload) {
  Block ret;
  ret.header = header;
  ret.payload.u64 = payload;
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
               HeaderBlockFields::Order::Make(0) |
               HeaderBlockFields::Version::Make(inspect::internal::kVersion);
  memcpy(&ret.header_data[4], kMagicNumber, 4);
  ret.payload.u64 = generation;
  return ret;
}

Snapshot SnapshotAndScan(const zx::vmo& vmo,
                         fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>>* blocks,
                         size_t* free_blocks, size_t* allocated_blocks) {
  *free_blocks = *allocated_blocks = 0;

  Snapshot snapshot;
  Snapshot::Create(vmo, &snapshot);
  if (snapshot) {
    ScanBlocks(snapshot.data(), snapshot.size(), [&](BlockIndex index, const Block* block) {
      if (GetType(block) == BlockType::kFree) {
        *free_blocks += 1;
      } else {
        *allocated_blocks += 1;
      }
      blocks->insert(std::make_unique<ScannedBlock>(index, block));
      return true;
    });
  }
  return snapshot;
}

TEST(State, CreateAndCopy) {
  auto state = State::CreateWithSize(4096);
  ASSERT_TRUE(state);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  EXPECT_EQ(1u, allocated_blocks);
  EXPECT_EQ(8u, free_blocks);
  blocks.clear();

  zx::vmo copy;
  ASSERT_TRUE(state->Copy(&copy));

  snapshot = SnapshotAndScan(copy, &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  EXPECT_EQ(1u, allocated_blocks);
  EXPECT_EQ(8u, free_blocks);
}

TEST(State, CreateIntProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  IntProperty a = state->CreateIntProperty("a", 0, 0);
  IntProperty b = state->CreateIntProperty("b", 0, 0);
  IntProperty c = state->CreateIntProperty("c", 0, 0);

  a.Set(10);
  b.Add(5);
  b.Subtract(10);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(7u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(12));
  CompareBlock(blocks.find(1)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                ValueBlockFields::NameIndex::Make(2),
                            10));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(3)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                ValueBlockFields::NameIndex::Make(4),
                            -5));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "b\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(5)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                ValueBlockFields::NameIndex::Make(6),
                            0));
  CompareBlock(blocks.find(6)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "c\0\0\0\0\0\0\0"));
}

TEST(State, CreateUintProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  UintProperty a = state->CreateUintProperty("a", 0, 0);
  UintProperty b = state->CreateUintProperty("b", 0, 0);
  UintProperty c = state->CreateUintProperty("c", 0, 0);

  a.Set(10);
  b.Add(15);
  b.Subtract(10);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(7u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(12));
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         10));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(3)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                             ValueBlockFields::NameIndex::Make(4),
                         5));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "b\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(5)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                                ValueBlockFields::NameIndex::Make(6),
                            0));
  CompareBlock(blocks.find(6)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "c\0\0\0\0\0\0\0"));
}

TEST(State, CreateDoubleProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  DoubleProperty a = state->CreateDoubleProperty("a", 0, 0);
  DoubleProperty b = state->CreateDoubleProperty("b", 0, 0);
  DoubleProperty c = state->CreateDoubleProperty("c", 0, 0);

  a.Set(3.25);
  b.Add(0.5);
  b.Subtract(0.25);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(7u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(12));
  CompareBlock(blocks.find(1)->block,
               MakeDoubleBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                   ValueBlockFields::NameIndex::Make(2),
                               3.25));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(3)->block,
               MakeDoubleBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                   ValueBlockFields::NameIndex::Make(4),
                               0.25));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "b\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(5)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kDoubleValue) |
                                ValueBlockFields::NameIndex::Make(6),
                            0));
  CompareBlock(blocks.find(6)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "c\0\0\0\0\0\0\0"));
}

TEST(State, CreateBoolProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);
  BoolProperty t = state->CreateBoolProperty("t", 0, true);
  BoolProperty f = state->CreateBoolProperty("f", 0, false);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  EXPECT_EQ(5u, allocated_blocks);
  EXPECT_EQ(7u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));
  CompareBlock(blocks.find(1)->block,
               MakeBoolBlock(ValueBlockFields::Type::Make(BlockType::kBoolValue) |
                                 ValueBlockFields::NameIndex::Make(2),
                             true));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "t\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(3)->block,
               MakeBoolBlock(ValueBlockFields::Type::Make(BlockType::kBoolValue) |
                                 ValueBlockFields::NameIndex::Make(4),
                             false));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "f\0\0\0\0\0\0\0"));
}

TEST(State, CreateArrays) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  IntArray a = state->CreateIntArray("a", 0, 10, ArrayBlockFormat::kDefault);
  UintArray b = state->CreateUintArray("b", 0, 10, ArrayBlockFormat::kDefault);
  DoubleArray c = state->CreateDoubleArray("c", 0, 10, ArrayBlockFormat::kDefault);

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

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(7u, allocated_blocks);
  EXPECT_EQ(4u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(42));

  {
    CompareBlock(blocks.find(1)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "a\0\0\0\0\0\0\0"));
    CompareBlock(
        blocks.find(8)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::Order::Make(3) | ValueBlockFields::NameIndex::Make(1),
                  ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    int64_t a_array_values[] = {10, -10, -9, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(8)->block, a_array_values, 10);
  }

  {
    CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "b\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(16)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::Order::Make(3) | ValueBlockFields::NameIndex::Make(2),
                  ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    uint64_t b_array_values[] = {10, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(16)->block, b_array_values, 10);
  }

  {
    CompareBlock(blocks.find(3)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "c\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(24)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::Order::Make(3) | ValueBlockFields::NameIndex::Make(3),
                  ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    double c_array_values[] = {.25, .75, 0, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(24)->block, c_array_values, 10);
  }
}

TEST(State, CreateArrayChildren) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  Node root = state->CreateNode("root", 0);

  IntArray a = root.CreateIntArray("a", 10);
  UintArray b = root.CreateUintArray("b", 10);
  DoubleArray c = root.CreateDoubleArray("c", 10);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(9u, allocated_blocks);
  EXPECT_EQ(4u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(8));

  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                3));

  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));

  {
    CompareBlock(blocks.find(3)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "a\0\0\0\0\0\0\0"));
    CompareBlock(
        blocks.find(8)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(3),
                  ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    int64_t a_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(8)->block, a_array_values, 10);
  }

  {
    CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "b\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(16)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(4),
                  ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    uint64_t b_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(16)->block, b_array_values, 10);
  }

  {
    CompareBlock(blocks.find(5)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "c\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(24)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(5),
                  ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kDefault) |
                      ArrayBlockPayload::Count::Make(10)));
    double c_array_values[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CompareArray(blocks.find(24)->block, c_array_values, 10);
  }
}

TEST(State, CreateLinearHistogramChildren) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  Node root = state->CreateNode("root", 0);

  auto a = root.CreateLinearIntHistogram("a", 10 /*floor*/, 5 /*step_size*/, 6 /*buckets*/);
  auto b = root.CreateLinearUintHistogram("b", 10 /*floor*/, 5 /*step_size*/, 6 /*buckets*/);
  auto c = root.CreateLinearDoubleHistogram("c", 10 /*floor*/, 5 /*step_size*/, 6 /*buckets*/);

  // Test moving of underlying LinearHistogram type.
  {
    inspect::LinearIntHistogram temp;
    temp = std::move(a);
    a = std::move(temp);
  }

  a.Insert(0, 3);
  a.Insert(10);
  a.Insert(1000);
  a.Insert(21);

  b.Insert(0, 3);
  b.Insert(10);
  b.Insert(1000);
  b.Insert(21);

  c.Insert(0, 3);
  c.Insert(10);
  c.Insert(1000);
  c.Insert(21);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(9u, allocated_blocks);
  EXPECT_EQ(4u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(2 + 6 * 3 + 8 * 3));

  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                3));

  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));

  {
    CompareBlock(blocks.find(3)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "a\0\0\0\0\0\0\0"));
    CompareBlock(
        blocks.find(8)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(3),
                  ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kLinearHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <step_size>, <underflow>, <N buckets>..., <overflow>
    int64_t a_array_values[] = {10, 5, 3, 1, 0, 1, 0, 0, 0, 1};
    CompareArray(blocks.find(8)->block, a_array_values, 10);
  }

  {
    CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "b\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(16)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(4),
                  ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kLinearHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <step_size>, <underflow>, <N buckets>..., <overflow>
    uint64_t b_array_values[] = {10, 5, 3, 1, 0, 1, 0, 0, 0, 1};
    CompareArray(blocks.find(16)->block, b_array_values, 10);
  }

  {
    CompareBlock(blocks.find(5)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "c\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(24)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(5),
                  ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kLinearHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <step_size>, <underflow>, <N buckets>..., <overflow>
    double c_array_values[] = {10, 5, 3, 1, 0, 1, 0, 0, 0, 1};
    CompareArray(blocks.find(24)->block, c_array_values, 10);
  }
}

TEST(State, CreateExponentialHistogramChildren) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  Node root = state->CreateNode("root", 0);

  auto a = root.CreateExponentialIntHistogram("a", 1 /*floor*/, 1 /*initial_step*/,
                                              2 /*step_multiplier*/, 5 /*buckets*/);
  auto b = root.CreateExponentialUintHistogram("b", 1 /*floor*/, 1 /*initial_step*/,
                                               2 /*step_multiplier*/, 5 /*buckets*/);
  auto c = root.CreateExponentialDoubleHistogram("c", 1 /*floor*/, 1 /*initial_step*/,
                                                 2 /*step_multiplier*/, 5 /*buckets*/);

  // Test moving of underlying ExponentialHistogram type.
  {
    inspect::ExponentialIntHistogram temp;
    temp = std::move(a);
    a = std::move(temp);
  }

  a.Insert(0, 3);
  a.Insert(4);
  a.Insert(1000);
  a.Insert(30);

  b.Insert(0, 3);
  b.Insert(4);
  b.Insert(1000);
  b.Insert(30);

  c.Insert(0, 3);
  c.Insert(4);
  c.Insert(1000);
  c.Insert(30);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header and 2 for each metric.
  EXPECT_EQ(9u, allocated_blocks);
  EXPECT_EQ(4u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(2 + 8 * 3 + 8 * 3));

  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                3));

  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));

  {
    CompareBlock(blocks.find(3)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "a\0\0\0\0\0\0\0"));
    CompareBlock(
        blocks.find(8)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(3),
                  ArrayBlockPayload::EntryType::Make(BlockType::kIntValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kExponentialHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <initial_step>, <step_multipler>, <underflow>, <N buckets>..., <overflow>
    int64_t a_array_values[] = {1, 1, 2, 3, 0, 0, 1, 0, 0, 2};
    CompareArray(blocks.find(8)->block, a_array_values, 10);
  }

  {
    CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "b\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(16)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(4),
                  ArrayBlockPayload::EntryType::Make(BlockType::kUintValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kExponentialHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <initial_step>, <step_multipler>, <underflow>, <N buckets>..., <overflow>
    uint64_t b_array_values[] = {1, 1, 2, 3, 0, 0, 1, 0, 0, 2};
    CompareArray(blocks.find(16)->block, b_array_values, 10);
  }

  {
    CompareBlock(blocks.find(5)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                      NameBlockFields::Length::Make(1),
                                                  "c\0\0\0\0\0\0\0"));

    CompareBlock(
        blocks.find(24)->block,
        MakeBlock(ValueBlockFields::Type::Make(BlockType::kArrayValue) |
                      ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::Order::Make(3) |
                      ValueBlockFields::NameIndex::Make(5),
                  ArrayBlockPayload::EntryType::Make(BlockType::kDoubleValue) |
                      ArrayBlockPayload::Flags::Make(ArrayBlockFormat::kExponentialHistogram) |
                      ArrayBlockPayload::Count::Make(10)));
    // Array is:
    // <floor>, <initial_step>, <step_multipler>, <underflow>, <N buckets>..., <overflow>
    double c_array_values[] = {1, 1, 2, 3, 0, 0, 1, 0, 0, 2};
    CompareArray(blocks.find(24)->block, c_array_values, 10);
  }
}

TEST(State, CreateSmallProperties) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  std::vector<uint8_t> temp = {'8', '8', '8', '8', '8', '8', '8', '8'};
  StringProperty a = state->CreateStringProperty("a", 0, "Hello");
  ByteVectorProperty b = state->CreateByteVectorProperty("b", 0, temp);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 2 single extent properties (6)
  EXPECT_EQ(1u + 6u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Property a fits in the first 3 blocks (value, name, extent).
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(3) |
                             PropertyBlockPayload::TotalLength::Make(5)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  CompareBlock(blocks.find(3)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "Hello\0\0\0"));

  // Property b fits in the next 3 blocks (value, name, extent).

  CompareBlock(blocks.find(4)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(5),
                         PropertyBlockPayload::ExtentIndex::Make(6) |
                             PropertyBlockPayload::TotalLength::Make(8) |
                             PropertyBlockPayload::Flags::Make(PropertyBlockFormat::kBinary)));
  CompareBlock(blocks.find(5)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "b\0\0\0\0\0\0\0"));

  CompareBlock(blocks.find(6)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "88888888"));
}

TEST(State, CreateLargeSingleExtentProperties) {
  auto state = InitState(2 * 4096);  // Need to extend to 2 pages to store both properties.
  ASSERT_TRUE(state != NULL);

  char input[] = "abcdefg";
  size_t input_size = 7;
  std::vector<uint8_t> contents;
  contents.reserve(2040);
  for (int i = 0; i < 2040; i++) {
    contents.push_back(input[i % input_size]);
  }
  std::string str_contents(reinterpret_cast<const char*>(contents.data()), 2040);
  StringProperty a = state->CreateStringProperty("a", 0, str_contents);
  ByteVectorProperty b = state->CreateByteVectorProperty("b", 0, contents);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 2 single extent properties (6)
  EXPECT_EQ(1u + 6u, allocated_blocks);
  EXPECT_EQ(7u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Property a has the first 2 blocks for value and name, but needs a large block for the
  // contents.
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(128) |
                             PropertyBlockPayload::TotalLength::Make(2040)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(128)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                             ExtentBlockFields::Order::Make(kNumOrders - 1),
                         "abcdefga"));
  EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents.data(), 2040));

  // Property b has the next 2 blocks at the beginning for its value and name, but it claims
  // another large block for the extent.

  CompareBlock(blocks.find(3)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(4),
                         PropertyBlockPayload::ExtentIndex::Make(256) |
                             PropertyBlockPayload::TotalLength::Make(2040) |
                             PropertyBlockPayload::Flags::Make(PropertyBlockFormat::kBinary)));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "b\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(256)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                             ExtentBlockFields::Order::Make(kNumOrders - 1),
                         "abcdefga"));
  EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents.data(), 2040));
}

TEST(State, CreateMultiExtentProperty) {
  auto state = InitState(2 * 4096);  // Need 4 pages to store 12K of properties.
  ASSERT_TRUE(state != NULL);

  char input[] = "abcdefg";
  size_t input_size = 7;
  std::string contents;
  for (int i = 0; i < 6000; i++) {
    contents.push_back(input[i % input_size]);
  }
  StringProperty a = state->CreateStringProperty("a", 0, contents);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 1 property (2) with 3 extents (3)
  EXPECT_EQ(1u + 2u + 3u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(2));

  // Property a has the first 2 blocks for its value and name.
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(128) |
                             PropertyBlockPayload::TotalLength::Make(6000)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));
  // Extents are threaded between blocks 128, 256, and 384.
  CompareBlock(blocks.find(128)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                             ExtentBlockFields::Order::Make(kNumOrders - 1) |
                             ExtentBlockFields::NextExtentIndex::Make(256),
                         "abcdefga"));
  EXPECT_EQ(0, memcmp(blocks.find(128)->block->payload.data, contents.data(), 2040));
  CompareBlock(blocks.find(256)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                             ExtentBlockFields::Order::Make(kNumOrders - 1) |
                             ExtentBlockFields::NextExtentIndex::Make(384),
                         "defgabcd"));
  EXPECT_EQ(0, memcmp(blocks.find(256)->block->payload.data, contents.data() + 2040, 2040));
  CompareBlock(blocks.find(384)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent) |
                             ExtentBlockFields::Order::Make(kNumOrders - 1),
                         "gabcdefg"));
  EXPECT_EQ(0, memcmp(blocks.find(384)->block->payload.data, contents.data() + 2 * 2040,
                      6000 - 2 * 2040));
}

TEST(State, SetSmallStringProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  StringProperty a = state->CreateStringProperty("a", 0, "Hello");

  a.Set("World");

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 1 single extent property (3)
  EXPECT_EQ(1u + 3u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Property a fits in the first 3 blocks (value, name, extent).
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(3) |
                             PropertyBlockPayload::TotalLength::Make(5) |
                             PropertyBlockPayload::Flags::Make(PropertyBlockFormat::kUtf8)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  CompareBlock(blocks.find(3)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kExtent), "World\0\0\0"));
}

TEST(State, SetSmallBinaryProperty) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  ByteVectorProperty a = state->CreateByteVectorProperty("a", 0, {'a', 'b', 'c', 'd'});

  a.Set({'a', 'a', 'a', 'a'});

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 1 single extent property (3)
  EXPECT_EQ(1u + 3u, allocated_blocks);
  EXPECT_EQ(6u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Property a fits in the first 3 blocks (value, name, extent).
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(3) |
                             PropertyBlockPayload::TotalLength::Make(4) |
                             PropertyBlockPayload::Flags::Make(PropertyBlockFormat::kBinary)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  CompareBlock(blocks.find(3)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kExtent), "aaaa\0\0\0\0"));
}

TEST(State, SetLargeProperty) {
  auto state = InitState(2 * 4096);  // Need space for 6K of contents.
  ASSERT_TRUE(state != NULL);

  char input[] = "abcdefg";
  size_t input_size = 7;
  std::string contents;
  for (int i = 0; i < 6000; i++) {
    contents.push_back(input[i % input_size]);
  }

  StringProperty a = state->CreateStringProperty("a", 0, contents);

  a.Set("World");

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), 1 single extent property (3)
  EXPECT_EQ(1u + 3u, allocated_blocks);
  EXPECT_EQ(8u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Property a fits in the first 3 blocks (value, name, extent).
  CompareBlock(blocks.find(1)->block,
               MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                             ValueBlockFields::NameIndex::Make(2),
                         PropertyBlockPayload::ExtentIndex::Make(3) |
                             PropertyBlockPayload::TotalLength::Make(5)));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  CompareBlock(blocks.find(3)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "World\0\0\0"));
}

TEST(State, SetPropertyOutOfMemory) {
  auto state = InitState(16 * 1024);  // Only 16K of space, property will not fit.
  ASSERT_TRUE(state != NULL);

  std::vector<uint8_t> vec;
  for (int i = 0; i < 65000; i++) {
    vec.push_back('a');
  }

  ByteVectorProperty a = state->CreateByteVectorProperty("a", 0, vec);
  EXPECT_FALSE(bool(a));

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1) only, property failed to fit.
  EXPECT_EQ(1u, allocated_blocks);
  EXPECT_EQ(14u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(2));
}

TEST(State, CreateNodeHierarchy) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  Node root = state->CreateNode("objects", 0);
  auto req = root.CreateChild("requests");
  auto network = req.CreateUint("network", 10);
  auto wifi = req.CreateUint("wifi", 5);

  auto version = root.CreateString("version", "1.0beta2");

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), root (2), requests (2), 2 metrics (4), small property (3)
  EXPECT_EQ(1u + 2u + 2u + 4u + 3u, allocated_blocks);
  EXPECT_EQ(5u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(10));

  // Root object is at index 1.
  // It has 2 references (req and version).
  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                2));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(7),
                                                "objects\0"));

  // Requests object is at index 3.
  // It has 2 references (wifi and network).
  CompareBlock(
      blocks.find(3)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::NameIndex::Make(4),
                2));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(8),
                                                "requests"));

  // Network value
  CompareBlock(
      blocks.find(5)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                    ValueBlockFields::ParentIndex::Make(3) | ValueBlockFields::NameIndex::Make(6),
                10));
  CompareBlock(blocks.find(6)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(7),
                                                "network\0"));

  // Wifi value
  CompareBlock(
      blocks.find(7)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kUintValue) |
                    ValueBlockFields::ParentIndex::Make(3) | ValueBlockFields::NameIndex::Make(8),
                5));
  CompareBlock(blocks.find(8)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "wifi\0\0\0\0"));

  // Version property
  CompareBlock(
      blocks.find(9)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                    ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::NameIndex::Make(10),
                PropertyBlockPayload::ExtentIndex::Make(11) |
                    PropertyBlockPayload::TotalLength::Make(8)));
  CompareBlock(blocks.find(10)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                     NameBlockFields::Length::Make(7),
                                                 "version\0"));

  CompareBlock(blocks.find(11)->block,
               MakeBlock(ExtentBlockFields::Type::Make(BlockType::kExtent), "1.0beta2"));
}

TEST(State, TombstoneTest) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  std::unique_ptr<Node> requests;
  {
    // Root going out of scope causes a tombstone to be created,
    // but since requests is referencing it it will not be deleted.
    Node root = state->CreateNode("objects", 0);
    requests = std::make_unique<Node>(root.CreateChild("requests"));
    auto a = root.CreateInt("a", 1);
    auto b = root.CreateUint("b", 1);
    auto c = root.CreateDouble("c", 1);
  }

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), root tombstone (2), requests (2)
  EXPECT_EQ(1u + 2u + 2u, allocated_blocks);
  EXPECT_EQ(7u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(18));

  // Root object is at index 1, but has been tombstoned.
  // It has 1 reference (requests)
  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kTombstone) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                1));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(7),
                                                "objects\0"));
  CompareBlock(
      blocks.find(3)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::NameIndex::Make(4)));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(8),
                                                "requests"));
}

TEST(State, TombstoneCleanup) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  IntProperty metric = state->CreateIntProperty("a", 0, 0);

  Node root = state->CreateNode("root", 0);
  {
    Node child1 = state->CreateNode("child1", 0);
    Node child2 = child1.CreateChild("child2");

    {
      Node child = child1.CreateChild("this_is_a_child");
      std::unique_ptr<IntProperty> m;
      {
        Node new_child = root.CreateChild("child");
        m = std::make_unique<IntProperty>(new_child.CreateInt("value", -1));
      }
      auto temp = child.CreateString("temp", "test");
      m.reset();
    }
  }

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
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
  CompareBlock(blocks.find(0)->block, MakeHeader(14 * 2));

  // Property "a" is at index 1.
  CompareBlock(blocks.find(1)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                ValueBlockFields::ParentIndex::Make(0) |
                                ValueBlockFields::NameIndex::Make(2),
                            0));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  // Root object is at index 3.
  // It has 0 references since the children should be removed.
  CompareBlock(
      blocks.find(3)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(4)));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));
}

TEST(State, LinkTest) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  // root will be at block index 1
  Node root = state->CreateNode("root", 0);
  Link link = state->CreateLink("link", 1u /* root index */, "/test", LinkBlockDisposition::kChild);
  Link link2 =
      state->CreateLink("link2", 1u /* root index */, "/test", LinkBlockDisposition::kInline);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), root (2), link (3), link2 (3)
  EXPECT_EQ(1u + 2u + 3u + 3u, allocated_blocks);
  EXPECT_EQ(7u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(6));

  // Root node has 2 children.
  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                2));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));
  CompareBlock(
      blocks.find(3)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kLinkValue) |
                    ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::NameIndex::Make(4),
                LinkBlockPayload::ContentIndex::Make(5)));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "link\0\0\0\0"));
  CompareBlock(blocks.find(5)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(5),
                                                "/test\0\0\0"));
  CompareBlock(
      blocks.find(6)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kLinkValue) |
                    ValueBlockFields::ParentIndex::Make(1) | ValueBlockFields::NameIndex::Make(7),
                LinkBlockPayload::ContentIndex::Make(8) |
                    LinkBlockPayload::Flags::Make(LinkBlockDisposition::kInline)));
  CompareBlock(blocks.find(7)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(5),
                                                "link2\0\0\0"));
  CompareBlock(blocks.find(8)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(5),
                                                "/test\0\0\0"));
}

TEST(State, LinkContentsAllocationFailure) {
  auto state = InitState(4096);
  ASSERT_TRUE(state != NULL);

  // root will be at block index 1
  Node root = state->CreateNode("root", 0);
  std::string name(2000, 'a');
  Link link = state->CreateLink(name, 1u /* root index */, name, LinkBlockDisposition::kChild);

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  // Header (1), root (2).
  EXPECT_EQ(1u + 2u, allocated_blocks);
  EXPECT_EQ(7u, free_blocks);

  CompareBlock(blocks.find(0)->block, MakeHeader(4));

  // Root node has 0 children.
  CompareBlock(
      blocks.find(1)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                    ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(2),
                "\0\0\0\0\0\0\0\0"));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));
}

constexpr size_t kThreadTimes = 1024 * 10;

struct ThreadArgs {
  IntProperty* metric;
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
  Node* object = reinterpret_cast<Node*>(input);
  for (size_t i = 0; i < kThreadTimes; i++) {
    Node child = object->CreateChild("this_is_a_child");
    auto temp = child.CreateString("temp", "test");
  }
  return 0;
}

TEST(State, MultithreadingTest) {
  auto state = InitState(10 * 4096);
  ASSERT_TRUE(state != NULL);

  size_t per_thread_times_operation_count = 0;
  size_t other_operation_count = 0;

  other_operation_count += 1;  // create a
  IntProperty metric = state->CreateIntProperty("a", 0, 0);

  ThreadArgs adder{.metric = &metric, .value = 2, .add = true};
  ThreadArgs subtractor{.metric = &metric, .value = 1, .add = false};

  thrd_t add_thread, subtract_thread, child_thread_1, child_thread_2;

  other_operation_count += 1;  // create root
  Node root = state->CreateNode("root", 0);
  {
    other_operation_count += 2;  // create and delete
    Node child1 = state->CreateNode("child1", 0);
    other_operation_count += 2;  // create and delete
    Node child2 = child1.CreateChild("child2");

    per_thread_times_operation_count += 1;  // add metric
    thrd_create(&add_thread, ValueThread, &adder);

    per_thread_times_operation_count += 1;  // subtract metric
    thrd_create(&subtract_thread, ValueThread, &subtractor);

    per_thread_times_operation_count += 4;  // create child, create temp, delete both
    thrd_create(&child_thread_1, ChildThread, &child1);
    per_thread_times_operation_count += 4;  // create child, create temp, delete both
    thrd_create(&child_thread_2, ChildThread, &child2);

    per_thread_times_operation_count += 4;  // create child, create m, delete both;
    for (size_t i = 0; i < kThreadTimes; i++) {
      Node child = root.CreateChild("child");
      IntProperty m = child.CreateInt("value", -1);
    }
    thrd_join(add_thread, nullptr);
    thrd_join(subtract_thread, nullptr);
    thrd_join(child_thread_1, nullptr);
    thrd_join(child_thread_2, nullptr);
  }

  fbl::WAVLTree<BlockIndex, std::unique_ptr<ScannedBlock>> blocks;
  size_t free_blocks, allocated_blocks;
  auto snapshot = SnapshotAndScan(state->GetVmo(), &blocks, &free_blocks, &allocated_blocks);
  ASSERT_TRUE(snapshot);

  CompareBlock(
      blocks.find(0)->block,
      MakeHeader(kThreadTimes * per_thread_times_operation_count * 2 + other_operation_count * 2));

  // Property "a" is at index 1.
  // Its value should be equal to kThreadTimes since subtraction
  // should cancel out half of addition.
  CompareBlock(blocks.find(1)->block,
               MakeIntBlock(ValueBlockFields::Type::Make(BlockType::kIntValue) |
                                ValueBlockFields::ParentIndex::Make(0) |
                                ValueBlockFields::NameIndex::Make(2),
                            kThreadTimes));
  CompareBlock(blocks.find(2)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(1),
                                                "a\0\0\0\0\0\0\0"));

  // Root object is at index 3.
  // It has 0 references since the children should be removed.
  CompareBlock(
      blocks.find(3)->block,
      MakeBlock(ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                ValueBlockFields::ParentIndex::Make(0) | ValueBlockFields::NameIndex::Make(4)));
  CompareBlock(blocks.find(4)->block, MakeBlock(NameBlockFields::Type::Make(BlockType::kName) |
                                                    NameBlockFields::Length::Make(4),
                                                "root\0\0\0\0"));
}

TEST(State, OutOfOrderDeletion) {
  // Ensure that deleting properties after their parent does not cause a crash.
  auto state = State::CreateWithSize(4096);
  {
    auto root = state->CreateRootNode();

    inspect::StringProperty a, b, c;
    auto base = root.CreateChild("base");
    c = base.CreateString("c", "test");
    b = base.CreateString("b", "test");
    a = base.CreateString("a", "test");
    ASSERT_TRUE(!!base);
    ASSERT_TRUE(!!c);
    ASSERT_TRUE(!!b);
    ASSERT_TRUE(!!a);
  }
}

}  // namespace
