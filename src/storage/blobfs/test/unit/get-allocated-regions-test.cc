// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>
#include <id_allocator/id_allocator.h>

#include "allocator/allocator.h"
#include "utils.h"

namespace blobfs {
namespace {

using id_allocator::IdAllocator;

void MakeBitmapFrom(const fbl::Vector<uint8_t>& bit_vector, RawBitmap* out_bitmap) {
  ASSERT_EQ(out_bitmap->Reset(bit_vector.size()), ZX_OK);
  for (size_t i = 0; i < bit_vector.size(); i++) {
    if (bit_vector[i] == 1) {
      ASSERT_EQ(out_bitmap->Set(i, i + 1), ZX_OK);
    }
  }
}

TEST(GetAllocatedRegionsTest, Empty) {
  MockSpaceManager space_manager;
  std::unique_ptr<Allocator> allocator;
  InitializeAllocator(1, 1, &space_manager, &allocator);

  // GetAllocatedRegions should return an empty vector
  ASSERT_EQ(0ul, allocator->GetAllocatedRegions().size());
}

TEST(GetAllocatedRegionsTest, Full) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {1};
  MakeBitmapFrom(bit_vector, &block_map);

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_EQ(IdAllocator::Create(0, &id_allocator), ZX_OK);

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(1ul, regions.size());
  ASSERT_EQ(0ul, regions[0].offset);
  ASSERT_EQ(1ul, regions[0].length);
}

TEST(GetAllocatedRegionsTest, Fragmented) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {1, 0, 1, 0, 1};
  MakeBitmapFrom(bit_vector, &block_map);

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_EQ(IdAllocator::Create(0, &id_allocator), ZX_OK);

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(3ul, regions.size());
  ASSERT_EQ(0ul, regions[0].offset);
  ASSERT_EQ(1ul, regions[0].length);
  ASSERT_EQ(2ul, regions[1].offset);
  ASSERT_EQ(1ul, regions[1].length);
  ASSERT_EQ(4ul, regions[2].offset);
  ASSERT_EQ(1ul, regions[2].length);
}

TEST(GetAllocatedRegionsTest, Length) {
  MockSpaceManager space_manager;
  RawBitmap block_map;
  fzl::ResizeableVmoMapper node_map;

  fbl::Vector<uint8_t> bit_vector = {0, 1, 1, 0};
  MakeBitmapFrom(bit_vector, &block_map);

  std::unique_ptr<IdAllocator> id_allocator;
  ASSERT_EQ(IdAllocator::Create(0, &id_allocator), ZX_OK);

  Allocator allocator(&space_manager, std::move(block_map), std::move(node_map),
                      std::move(id_allocator));
  allocator.SetLogging(false);

  fbl::Vector<BlockRegion> regions = allocator.GetAllocatedRegions();
  ASSERT_EQ(1ul, regions.size());
  ASSERT_EQ(1ul, regions[0].offset);
  ASSERT_EQ(2ul, regions[0].length);
}

}  // namespace
}  // namespace blobfs
