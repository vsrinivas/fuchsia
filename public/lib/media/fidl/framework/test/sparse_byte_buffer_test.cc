// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/application/application_test_base.h"
#include "services/media/framework/parts/sparse_byte_buffer.h"

namespace mojo {
namespace media {

class SparseByteBufferTest : public test::ApplicationTestBase {
 public:
  static const size_t kSize = 1000u;

  SparseByteBufferTest() { under_test_.Initialize(kSize); }

  void ExpectNullRegion(SparseByteBuffer::Region region) {
    EXPECT_EQ(under_test_.null_region(), region);
  }

  void ExpectRegion(size_t position,
                    size_t size,
                    SparseByteBuffer::Region region) {
    EXPECT_NE(under_test_.null_region(), region);
    EXPECT_EQ(position, region.position());
    EXPECT_EQ(size, region.size());
    uint8_t* data = region.data();
    EXPECT_NE(nullptr, data);
    for (size_t i = 0; i < size; i++) {
      EXPECT_EQ(data[i], ByteForPosition(position + i));
    }
  }

  void ExpectNullHole(SparseByteBuffer::Hole hole) {
    EXPECT_EQ(under_test_.null_hole(), hole);
  }

  void ExpectHole(size_t position, size_t size, SparseByteBuffer::Hole hole) {
    EXPECT_NE(under_test_.null_hole(), hole);
    EXPECT_EQ(position, hole.position());
    EXPECT_EQ(size, hole.size());
  }

  uint8_t ByteForPosition(size_t position) {
    return static_cast<uint8_t>(position ^ (position >> 8) ^ (position >> 16) ^
                                (position >> 24));
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
    under_test_.Initialize(kSize);

    size_t hole_size = kSize / hole_count;

    // Create the holes.
    for (size_t position = 0; position < kSize; position += hole_size) {
      SparseByteBuffer::Hole hole =
          under_test_.FindOrCreateHole(position, under_test_.null_hole());
      ExpectHole(position, kSize - position, hole);
    }

    // Use the indicated hole as a hint.
    SparseByteBuffer::Hole hint = under_test_.FindHoleContaining(hint_position);

    // Run FindOrCreateHole against every position for each hint.
    for (size_t position = 0; position < kSize; position++) {
      SparseByteBuffer::Hole hole =
          under_test_.FindOrCreateHole(position, hint);
      size_t expected_size = hole_size - position % hole_size;
      if (position + expected_size > kSize) {
        expected_size = kSize - position;
      }
      ExpectHole(position, expected_size, hole);
    }
  }

  SparseByteBuffer under_test_;
};

// Tests that the buffer behaves as expected immediately after initialization.
TEST_F(SparseByteBufferTest, InitialState) {
  // Null comparison works for regions.
  ExpectNullRegion(under_test_.null_region());

  // No regions anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullRegion(
        under_test_.FindRegionContaining(position, under_test_.null_region()));
  }

  // Null comparison works for holes.
  ExpectNullHole(under_test_.null_hole());

  // One hole everywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectHole(0, kSize, under_test_.FindHoleContaining(position));
  }

  // FindOrCreateHole finds the hole.
  ExpectHole(0, kSize,
             under_test_.FindOrCreateHole(0, under_test_.null_hole()));
}

// Creates a second hole.
TEST_F(SparseByteBufferTest, TwoHoles) {
  // Create a hole from kSize/2 to the end.
  SparseByteBuffer::Hole created_hole =
      under_test_.FindOrCreateHole(kSize / 2, under_test_.null_hole());

  // One hole covers the first half.
  for (size_t position = 0; position < kSize / 2; position++) {
    ExpectHole(0, kSize / 2, under_test_.FindHoleContaining(position));
  }

  // Created hole covers the second half.
  for (size_t position = kSize / 2; position < kSize; position++) {
    EXPECT_EQ(created_hole, under_test_.FindHoleContaining(position));
  }

  // No regions anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullRegion(
        under_test_.FindRegionContaining(position, under_test_.null_region()));
  }
}

// Creates a single region that covers the entire buffer.
TEST_F(SparseByteBufferTest, BigRegion) {
  // Fill the whole buffer. No hole should be returned, because there are no
  // holes anymore.
  ExpectNullHole(under_test_.Fill(under_test_.FindHoleContaining(0),
                                  CreateBuffer(0, kSize)));

  // Find the new region.
  SparseByteBuffer::Region big_region =
      under_test_.FindRegionContaining(0, under_test_.null_region());
  ExpectRegion(0, kSize, big_region);

  // Same region everywhere.
  for (size_t position = 0; position < kSize; position++) {
    EXPECT_EQ(big_region, under_test_.FindRegionContaining(
                              position, under_test_.null_region()));
  }

  // No holes anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullHole(under_test_.FindHoleContaining(position));
  }
}

// Creates a region for every other byte, then fills in the holes.
TEST_F(SparseByteBufferTest, TinyRegions) {
  // Create the regions.
  for (size_t position = 0; position < kSize; position += 2) {
    SparseByteBuffer::Hole hole =
        under_test_.FindOrCreateHole(position, under_test_.null_hole());
    ExpectHole(position, kSize - position, hole);
    ExpectHole(position + 1, kSize - position - 1,
               under_test_.Fill(hole, CreateBuffer(position, 1)));
  }

  // Find them again.
  for (size_t position = 0; position < kSize; position += 2) {
    ExpectRegion(position, 1, under_test_.FindRegionContaining(
                                  position, under_test_.null_region()));
  }

  // Find the holes.
  for (size_t position = 1; position < kSize; position += 2) {
    ExpectHole(position, 1, under_test_.FindHoleContaining(position));
  }

  // Fill in the holes.
  for (size_t position = 1; position < kSize; position += 2) {
    SparseByteBuffer::Hole hole = under_test_.FindHoleContaining(position);
    ExpectHole(position, 1, hole);
    hole = under_test_.Fill(hole, CreateBuffer(position, 1));
    if (position + 2 < kSize) {
      ExpectHole(position + 2, 1, hole);
    } else {
      ExpectNullHole(hole);
    }
  }

  // Tiny regions everywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectRegion(position, 1, under_test_.FindRegionContaining(
                                  position, under_test_.null_region()));
  }

  // No holes anywhere.
  for (size_t position = 0; position < kSize; position++) {
    ExpectNullHole(under_test_.FindHoleContaining(position));
  }
}

// Verifies that FindRegionContaining works regardless of the hints it's given.
TEST_F(SparseByteBufferTest, RegionHints) {
  static const size_t region_count = 11u;
  size_t region_size = kSize / region_count;

  // Create the regions.
  for (size_t position = 0; position < kSize; position += region_size) {
    SparseByteBuffer::Hole hole =
        under_test_.FindOrCreateHole(position, under_test_.null_hole());
    ExpectHole(position, kSize - position, hole);
    if (position + region_size >= kSize) {
      ExpectNullHole(
          under_test_.Fill(hole, CreateBuffer(position, kSize - position)));
    } else {
      ExpectHole(position + region_size, kSize - position - region_size,
                 under_test_.Fill(hole, CreateBuffer(position, region_size)));
    }
  }

  // Use each region as a hint.
  for (size_t hint_position = 0; hint_position < kSize;
       hint_position += region_size) {
    SparseByteBuffer::Region hint = under_test_.FindRegionContaining(
        hint_position, under_test_.null_region());
    // Run FindRegionContaining against every position for each hint.
    for (size_t position = 0; position < kSize; position++) {
      SparseByteBuffer::Region region =
          under_test_.FindRegionContaining(position, hint);
      size_t region_position = position - (position % region_size);
      ExpectRegion(region_position, region_position + region_size > kSize
                                        ? kSize - region_position
                                        : region_size,
                   region);
    }
  }

  // Make sure null_region works as a hint.
  for (size_t position = 0; position < kSize; position++) {
    SparseByteBuffer::Region region =
        under_test_.FindRegionContaining(position, under_test_.null_region());
    size_t region_position = position - (position % region_size);
    ExpectRegion(region_position,
                 region_position + region_size > kSize ? kSize - region_position
                                                       : region_size,
                 region);
  }
}

// Verifies that FindOrCreateHole works regardless of the hints it's given.
TEST_F(SparseByteBufferTest, HoleHints) {
  static const size_t hole_count = 11u;
  size_t hole_size = kSize / hole_count;

  for (size_t hint_position = 0; hint_position < kSize;
       hint_position += hole_size) {
    VerifyHoleHint(hole_count, hint_position);
  }
}

}  // namespace media
}  // namespace mojo
