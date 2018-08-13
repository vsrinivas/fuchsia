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

}  // namespace
}  // namespace media_player
