// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <id_allocator/id_allocator.h>
#include <zxtest/zxtest.h>

#include "allocator/allocator.h"
#include "utils.h"

namespace blobfs {
namespace {

using id_allocator::IdAllocator;

void MakeBitmapFrom(const fbl::Vector<uint8_t>& bit_vector, RawBitmap* out_bitmap) {
  ASSERT_OK(out_bitmap->Reset(bit_vector.size()));
  for (size_t i = 0; i < bit_vector.size(); i++) {
    if (bit_vector[i] == 1) {
      ASSERT_OK(out_bitmap->Set(i, i + 1));
    }
  }
}

TEST(GetAllocatedRegionsTest, Empty) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FAILURES(InitializeAllocator(1, 1, &space_manager, &allocator));

  // GetAllocatedRegions should return an empty vector
  ASSERT_EQ(0, allocator->GetAllocatedRegions().size());
}

TEST(GetAllocatedRegionsTest, Full) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {1};
  ASSERT_NO_FAILURES(MakeBitmapFrom(bit_vector, &block_map));

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_OK(IdAllocator::Create(0, &id_allocator));

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(1, regions.size());
  ASSERT_EQ(0, regions[0].offset);
  ASSERT_EQ(1, regions[0].length);
}

TEST(GetAllocatedRegionsTest, Fragmented) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {1, 0, 1, 0, 1};
  ASSERT_NO_FAILURES(MakeBitmapFrom(bit_vector, &block_map));

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_OK(IdAllocator::Create(0, &id_allocator));

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(3, regions.size());
  ASSERT_EQ(0, regions[0].offset);
  ASSERT_EQ(1, regions[0].length);
  ASSERT_EQ(2, regions[1].offset);
  ASSERT_EQ(1, regions[1].length);
  ASSERT_EQ(4, regions[2].offset);
  ASSERT_EQ(1, regions[2].length);
}

TEST(GetAllocatedRegionsTest, Length) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {0, 1, 1, 0};
  ASSERT_NO_FAILURES(MakeBitmapFrom(bit_vector, &block_map));

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_OK(IdAllocator::Create(0, &id_allocator));

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(1, regions.size());
  ASSERT_EQ(1, regions[0].offset);
  ASSERT_EQ(2, regions[0].length);
}

}  // namespace
}  // namespace blobfs
