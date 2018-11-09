// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/sparse_byte_buffer.h"

#include "gtest/gtest.h"

namespace media_player {
namespace {

static const size_t kSize = 1000u;

void ExpectNullRegion(SparseByteBuffer* under_test,
                      SparseByteBuffer::Region region) {
  EXPECT_EQ(under_test->null_region(), region);
}

uint8_t ByteForPosition(size_t position) {
  return static_cast<uint8_t>(position ^ (position >> 8) ^ (position >> 16) ^
                              (position >> 24));
}

void ExpectRegion(SparseByteBuffer* under_test, size_t position, size_t size,
                  SparseByteBuffer::Region region) {
  EXPECT_NE(under_test->null_region(), region);
  EXPECT_EQ(position, region.position());
  EXPECT_EQ(size, region.size());
  uint8_t* data = region.data();
  EXPECT_NE(nullptr, data);
  for (size_t i = 0; i < size; i++) {
    EXPECT_EQ(data[i], ByteForPosition(position + i));
  }
}

void ExpectNullHole(SparseByteBuffer* under_test, SparseByteBuffer::Hole hole) {
  EXPECT_EQ(under_test->null_hole(), hole);
}

void ExpectHole(SparseByteBuffer* under_test, size_t position, size_t size,
                SparseByteBuffer::Hole hole) {
  EXPECT_NE(SparseByteBuffer::Hole(), hole);
  EXPECT_NE(under_test->null_hole(), hole);
  EXPECT_EQ(position, hole.position());
  EXPECT_EQ(size, hole.size());
}

std::vector<uint8_t> CreateBuffer(size_t position, size_t size) {
  std::vector<uint8_t> buffer(size);
  for (size_t i = 0; i < size; i++) {
    buffer[i] = ByteForPosition(i + position);
  }
  return buffer;
}

void FillRegion(SparseByteBuffer* under_test, size_t start, size_t size) {
  SparseByteBuffer::Hole hole_to_fill =
      under_test->FindOrCreateHole(start, under_test->null_hole());
  if (hole_to_fill == under_test->null_hole()) {
    return;
  }

  under_test->Fill(hole_to_fill, CreateBuffer(start, size));
}

SparseByteBuffer BufferWithRegions(
    std::vector<std::pair<size_t, size_t>> regions) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  for (const auto& region_spec : regions) {
    FillRegion(&under_test, region_spec.first, region_spec.second);
  }

  return under_test;
}

// See HoleHints test.
void VerifyHoleHint(size_t hole_count, size_t hint_position) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  size_t hole_size = kSize / hole_count;

  // Create the holes.
  for (size_t position = 0; position < kSize; position += hole_size) {
    SparseByteBuffer::Hole hole =
        under_test.FindOrCreateHole(position, under_test.null_hole());
    ExpectHole(&under_test, position, kSize - position, hole);
  }

  // Use the indicated hole as a hint.
  SparseByteBuffer::Hole hint = under_test.FindHoleContaining(hint_position);

  // Run FindOrCreateHole against every position for each hint.
  for (size_t position = 0; position < kSize; position++) {
    SparseByteBuffer::Hole hole = under_test.FindOrCreateHole(position, hint);
    size_t expected_size = hole_size - position % hole_size;
    if (position + expected_size > kSize) {
      expected_size = kSize - position;
    }
    ExpectHole(&under_test, position, expected_size, hole);
  }
}

// Tests that the buffer behaves as expected immediately after initialization.
TEST(SparseByteBufferTest, InitialState) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  // Null comparison works for regions.
  ExpectNullRegion(&under_test, under_test.null_region());

  // No regions anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullRegion(&under_test, under_test.FindRegionContaining(
                                      position, under_test.null_region()));
  }

  // Null comparison works for holes.
  ExpectNullHole(&under_test, under_test.null_hole());

  // One hole everywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectHole(&under_test, 0, kSize, under_test.FindHoleContaining(position));
  }

  // FindOrCreateHole finds the hole.
  ExpectHole(&under_test, 0, kSize,
             under_test.FindOrCreateHole(0, under_test.null_hole()));
}

TEST(SparseByteBufferTest, ReadRange) {
  {
    // Read range filled with regions.
    SparseByteBuffer under_test = BufferWithRegions({{0, 100}, {100, 200}});
    std::vector<uint8_t> dest_buffer(200, 0);
    size_t copied = under_test.ReadRange(0, 200, dest_buffer.data());
    EXPECT_EQ(copied, 200u);
    EXPECT_EQ(dest_buffer, CreateBuffer(0, 200));
  }

  {
    // Read range from region stretching beyond it.
    SparseByteBuffer under_test = BufferWithRegions({{0, 1000}});
    std::vector<uint8_t> dest_buffer(50, 0);
    size_t copied = under_test.ReadRange(100, 50, dest_buffer.data());
    EXPECT_EQ(copied, 50u);
    EXPECT_EQ(dest_buffer, CreateBuffer(100, 50));
  }

  {
    // Read range with only partial coverage.
    SparseByteBuffer under_test = BufferWithRegions({{0, 50}});
    std::vector<uint8_t> dest_buffer(25);
    size_t copied = under_test.ReadRange(25, 50, dest_buffer.data());
    EXPECT_EQ(copied, 25u);
    EXPECT_EQ(dest_buffer, CreateBuffer(25, 25));
  }

  {
    // Read range with only partial coverage that should bail early.
    SparseByteBuffer under_test = BufferWithRegions({{0, 50}, {100, 50}});
    std::vector<uint8_t> dest_buffer(25);
    size_t copied = under_test.ReadRange(25, 500, dest_buffer.data());
    EXPECT_EQ(copied, 25u);
    EXPECT_EQ(dest_buffer, CreateBuffer(25, 25));
  }
}

// Creates a second hole.
TEST(SparseByteBufferTest, TwoHoles) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  // Create a hole from kSize/2 to the end.
  SparseByteBuffer::Hole created_hole =
      under_test.FindOrCreateHole(kSize / 2, under_test.null_hole());

  // One hole covers the first half.
  for (size_t position = 0; position < kSize / 2; position++) {
    ExpectHole(&under_test, 0, kSize / 2,
               under_test.FindHoleContaining(position));
  }

  // Created hole covers the second half.
  for (size_t position = kSize / 2; position < kSize; position++) {
    EXPECT_EQ(created_hole, under_test.FindHoleContaining(position));
  }

  // No regions anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullRegion(&under_test, under_test.FindRegionContaining(
                                      position, under_test.null_region()));
  }
}

// Creates a single region that covers the entire buffer.
TEST(SparseByteBufferTest, BigRegion) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  // Fill the whole buffer. No hole should be returned, because there are no
  // holes anymore.
  ExpectNullHole(&under_test, under_test.Fill(under_test.FindHoleContaining(0),
                                              CreateBuffer(0, kSize)));

  // Find the new region.
  SparseByteBuffer::Region big_region =
      under_test.FindRegionContaining(0, under_test.null_region());
  ExpectRegion(&under_test, 0, kSize, big_region);

  // Same region everywhere.
  for (size_t position = 0; position < kSize; position++) {
    EXPECT_EQ(big_region, under_test.FindRegionContaining(
                              position, under_test.null_region()));
  }

  // No holes anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullHole(&under_test, under_test.FindHoleContaining(position));
  }
}

// Creates a region for every other byte, then fills in the holes.
TEST(SparseByteBufferTest, TinyRegions) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  // Create the regions.
  for (size_t position = 0; position < kSize; position += 2) {
    SparseByteBuffer::Hole hole =
        under_test.FindOrCreateHole(position, under_test.null_hole());
    ExpectHole(&under_test, position, kSize - position, hole);
    ExpectHole(&under_test, position + 1, kSize - position - 1,
               under_test.Fill(hole, CreateBuffer(position, 1)));
  }

  // Find them again.
  for (size_t position = 0; position < kSize; position += 2) {
    ExpectRegion(
        &under_test, position, 1,
        under_test.FindRegionContaining(position, under_test.null_region()));
  }

  // Find the holes.
  for (size_t position = 1; position < kSize; position += 2) {
    ExpectHole(&under_test, position, 1,
               under_test.FindHoleContaining(position));
  }

  // Fill in the holes.
  for (size_t position = 1; position < kSize; position += 2) {
    SparseByteBuffer::Hole hole = under_test.FindHoleContaining(position);
    ExpectHole(&under_test, position, 1, hole);
    hole = under_test.Fill(hole, CreateBuffer(position, 1));
    if (position + 2 < kSize) {
      ExpectHole(&under_test, position + 2, 1, hole);
    } else {
      ExpectNullHole(&under_test, hole);
    }
  }

  // Tiny regions everywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectRegion(
        &under_test, position, 1,
        under_test.FindRegionContaining(position, under_test.null_region()));
  }

  // No holes anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullHole(&under_test, under_test.FindHoleContaining(position));
  }
}

// Verifies that FindRegionContaining works regardless of the hints it's given.
TEST(SparseByteBufferTest, RegionHints) {
  SparseByteBuffer under_test;
  under_test.Initialize(kSize);

  static const size_t region_count = 11u;
  size_t region_size = kSize / region_count;

  // Create the regions.
  for (size_t position = 0; position < kSize; position += region_size) {
    SparseByteBuffer::Hole hole =
        under_test.FindOrCreateHole(position, under_test.null_hole());
    ExpectHole(&under_test, position, kSize - position, hole);
    if (position + region_size >= kSize) {
      ExpectNullHole(
          &under_test,
          under_test.Fill(hole, CreateBuffer(position, kSize - position)));
    } else {
      ExpectHole(&under_test, position + region_size,
                 kSize - position - region_size,
                 under_test.Fill(hole, CreateBuffer(position, region_size)));
    }
  }

  // Use each region as a hint.
  for (size_t hint_position = 0; hint_position < kSize;
       hint_position += region_size) {
    SparseByteBuffer::Region hint = under_test.FindRegionContaining(
        hint_position, under_test.null_region());
    // Run FindRegionContaining against every position for each hint.
    for (size_t position = 0; position < kSize; position++) {
      SparseByteBuffer::Region region =
          under_test.FindRegionContaining(position, hint);
      size_t region_position = position - (position % region_size);
      ExpectRegion(&under_test, region_position,
                   region_position + region_size > kSize
                       ? kSize - region_position
                       : region_size,
                   region);
    }
  }

  // Make sure null_region works as a hint.
  for (size_t position = 0; position < kSize; position++) {
    SparseByteBuffer::Region region =
        under_test.FindRegionContaining(position, under_test.null_region());
    size_t region_position = position - (position % region_size);
    ExpectRegion(&under_test, region_position,
                 region_position + region_size > kSize ? kSize - region_position
                                                       : region_size,
                 region);
  }
}

// Verifies that FindOrCreateHole works regardless of the hints it's given.
TEST(SparseByteBufferTest, HoleHints) {
  static const size_t hole_count = 11u;
  size_t hole_size = kSize / hole_count;

  for (size_t hint_position = 0; hint_position < kSize;
       hint_position += hole_size) {
    VerifyHoleHint(hole_count, hint_position);
  }
}

TEST(SparseByteBufferTest, FindOrCreateHolesInRange) {
  {
    // Test buffer diagram:
    //       | Selected Range |
    // [   =   ==== ===      ==== ....]
    // Regions (corresponding to diagram):
    SparseByteBuffer under_test =
        BufferWithRegions({{3, 1}, {7, 4}, {12, 3}, {21, 4}});

    std::vector<SparseByteBuffer::Hole> holes =
        under_test.FindOrCreateHolesInRange(5, 17);

    // Expected holes in range (corresponding to diagram):
    EXPECT_EQ(holes.size(), 3u) << "Number of holes vs Expected";
    ExpectHole(&under_test, 5, 2, holes[0]);
    ExpectHole(&under_test, 11, 1, holes[1]);
    ExpectHole(&under_test, 15, 6, holes[2]);

    // Expected holes outside of range (corresponding to diagram):
    ExpectHole(&under_test, 0, 3, under_test.FindHoleContaining(0));
    ExpectHole(&under_test, 4, 1, under_test.FindHoleContaining(4));
    ExpectHole(&under_test, 25, kSize - 25, under_test.FindHoleContaining(25));
  }

  {
    // Find hole on end of range.
    SparseByteBuffer under_test = BufferWithRegions({{0, 100}});
    std::vector<SparseByteBuffer::Hole> holes =
        under_test.FindOrCreateHolesInRange(50, 100);

    EXPECT_EQ(holes.size(), 1u);
    ExpectHole(&under_test, 100, 50, holes[0]);
    ExpectHole(&under_test, 150, kSize - 150,
               under_test.FindHoleContaining(150));
  }

  {
    // Find hole in empty buffer.
    SparseByteBuffer under_test = BufferWithRegions({});
    std::vector<SparseByteBuffer::Hole> holes =
        under_test.FindOrCreateHolesInRange(100, 100);

    EXPECT_EQ(holes.size(), 1u);
    ExpectHole(&under_test, 100, 100, holes[0]);
    ExpectHole(&under_test, 0, 100, under_test.FindHoleContaining(0));
    ExpectHole(&under_test, 200, kSize - 200,
               under_test.FindHoleContaining(200));
  }
}

TEST(SparseByteBufferTest, BytesMissingInRange) {
  {
    // Test buffer diagram:
    //       | Selected Range |
    // [   =   ==== ===      ==== ....]
    // Regions (corresponding to diagram):
    SparseByteBuffer under_test =
        BufferWithRegions({{3, 1}, {7, 4}, {12, 3}, {21, 4}});

    size_t bytes_missing = under_test.BytesMissingInRange(5, 17);

    // Expected bytes missing in range (corresponding to diagram):
    EXPECT_EQ(bytes_missing, 9u) << "Number of missing bytes vs Expected";
  }

  {
    // Find missing bytes in end of range.
    SparseByteBuffer under_test = BufferWithRegions({{0, 100}});
    size_t bytes_missing = under_test.BytesMissingInRange(50, 100);

    EXPECT_EQ(bytes_missing, 50u);
  }

  {
    // Find missing bytes in empty buffer.
    SparseByteBuffer under_test = BufferWithRegions({});
    size_t bytes_missing = under_test.BytesMissingInRange(100, 100);

    EXPECT_EQ(bytes_missing, 100u);
  }
}

TEST(SparseByteBufferTest, FreeRegion) {
  {
    // Free a region with a hole before and after it.
    SparseByteBuffer under_test = BufferWithRegions({{40, 50}});
    SparseByteBuffer::Region region_to_free =
        under_test.FindRegionContaining(40, under_test.null_region());
    SparseByteBuffer::Hole enclosing_hole = under_test.Free(region_to_free);

    ExpectHole(&under_test, 0, kSize, enclosing_hole);
  }

  {
    // Free a region at the buffer beginning.
    SparseByteBuffer under_test = BufferWithRegions({{0, 20}});
    SparseByteBuffer::Region region_to_free =
        under_test.FindRegionContaining(0, under_test.null_region());
    SparseByteBuffer::Hole enclosing_hole = under_test.Free(region_to_free);

    ExpectHole(&under_test, 0, kSize, enclosing_hole);
  }

  {
    // Free a sandwiched Region.
    SparseByteBuffer under_test =
        BufferWithRegions({{10, 10}, {20, 10}, {30, 10}});

    SparseByteBuffer::Region region_to_free =
        under_test.FindRegionContaining(20, under_test.null_region());
    SparseByteBuffer::Hole enclosing_hole = under_test.Free(region_to_free);

    ExpectHole(&under_test, 20, 10, enclosing_hole);
  }

  {
    // Free region with holes not adjacent.
    SparseByteBuffer under_test =
        BufferWithRegions({{10, 10}, {20, 10}, {30, 10}, {50, 10}, {90, 10}});

    SparseByteBuffer::Region region_to_free =
        under_test.FindRegionContaining(20, under_test.null_region());
    SparseByteBuffer::Hole enclosing_hole = under_test.Free(region_to_free);

    ExpectHole(&under_test, 20, 10, enclosing_hole);
  }
}

TEST(SparseByteBufferTest, CleanUpExcept) {
  {
    // Flagship usecase.
    // Clean up except a space with regions on the border.
    // Visually:
    //      | Protected Range |
    // [  ====   ====    ========  ==== ...]
    //
    // Regions (corresponding to diagram):
    SparseByteBuffer under_test =
        BufferWithRegions({{2, 4}, {9, 4}, {17, 8}, {27, 4}});
    // Protected range:
    size_t protected_start = 4;
    size_t protected_size = 19;

    size_t freed = under_test.CleanUpExcept(
        /* >= the buffer size so it cleans the most it can */ kSize,
        protected_start, protected_size);

    // Full expectation layout corresponding to diagram.
    EXPECT_EQ(freed, 8u);
    ExpectHole(&under_test, 0, 4, under_test.FindHoleContaining(0));
    ExpectRegion(&under_test, 4, 2,
                 under_test.FindRegionContaining(4, under_test.null_region()));
    ExpectHole(&under_test, 6, 3, under_test.FindHoleContaining(6));
    ExpectRegion(&under_test, 9, 4,
                 under_test.FindRegionContaining(9, under_test.null_region()));
    ExpectHole(&under_test, 13, 4, under_test.FindHoleContaining(13));
    ExpectRegion(&under_test, 17, 6,
                 under_test.FindRegionContaining(17, under_test.null_region()));
    ExpectHole(&under_test, 23, kSize - 23, under_test.FindHoleContaining(23));
  }

  {
    // Clean up less than available excess; should truncate excess region after
    // protected range.
    SparseByteBuffer under_test = BufferWithRegions({{0, 100}, {900, 100}});
    size_t freed = under_test.CleanUpExcept(50, 0, 100);

    EXPECT_EQ(freed, 50u);
    ExpectRegion(
        &under_test, 900, 50,
        under_test.FindRegionContaining(900, under_test.null_region()));
    ExpectHole(&under_test, 950, 50, under_test.FindHoleContaining(950));
  }

  {
    // Clean up a buffer with no regions; ensure graceful return.
    SparseByteBuffer under_test;
    under_test.Initialize(100);
    EXPECT_EQ(under_test.CleanUpExcept(100, 0, 10), 0u);
  }
}

TEST(SparseByteBufferTest, ShrinkRegionFront) {
  {
    // Shrink a region with a hole before it.
    SparseByteBuffer under_test = BufferWithRegions({{1, 4}});
    SparseByteBuffer::Region result = under_test.ShrinkRegionFront(
        under_test.FindRegionContaining(1, under_test.null_region()), 1);
    ExpectRegion(&under_test, 2, 3, result);
    ExpectHole(&under_test, 0, 2, under_test.FindHoleContaining(0));
  }

  {
    // Shrink a region with a region before it.
    SparseByteBuffer under_test = BufferWithRegions({{2, 4}, {6, 4}});
    SparseByteBuffer::Region result = under_test.ShrinkRegionFront(
        under_test.FindRegionContaining(6, under_test.null_region()), 2);
    ExpectRegion(&under_test, 8, 2, result);
    ExpectHole(&under_test, 6, 2, under_test.FindHoleContaining(6));
  }

  {
    // Shrink a region totally (free it).
    SparseByteBuffer under_test = BufferWithRegions({{2, 4}, {6, 4}, {10, 2}});
    under_test.ShrinkRegionFront(
        under_test.FindRegionContaining(6, under_test.null_region()), 4);
    ExpectHole(&under_test, 6, 4, under_test.FindHoleContaining(6));
  }
}

TEST(SparseByteBufferTest, ShrinkRegionBack) {
  {
    // Shrink a region with a hole after it.
    SparseByteBuffer under_test = BufferWithRegions({{0, 4}});
    SparseByteBuffer::Region result = under_test.ShrinkRegionBack(
        under_test.FindRegionContaining(0, under_test.null_region()), 1);
    ExpectRegion(&under_test, 0, 3, result);
    ExpectHole(&under_test, 3, kSize - 3, under_test.FindHoleContaining(3));
  }

  {
    // Shrink a region with a region after it.
    SparseByteBuffer under_test = BufferWithRegions({{2, 4}, {6, 4}});
    SparseByteBuffer::Region result = under_test.ShrinkRegionBack(
        under_test.FindRegionContaining(2, under_test.null_region()), 2);
    ExpectRegion(&under_test, 2, 2, result);
    ExpectHole(&under_test, 4, 2, under_test.FindHoleContaining(4));
  }

  {
    // Shrink a region totally (free it).
    SparseByteBuffer under_test = BufferWithRegions({{2, 4}, {6, 4}, {10, 2}});
    under_test.ShrinkRegionBack(
        under_test.FindRegionContaining(6, under_test.null_region()), 4);
    ExpectHole(&under_test, 6, 4, under_test.FindHoleContaining(6));
  }
}

}  // namespace
}  // namespace media_player
