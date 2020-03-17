// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/buffer_allocator.h>
#include <lib/fidl/llcpp/memory.h>

#include "gtest/gtest.h"

TEST(Memory, TrackingPointerUnowned) {
  uint32_t obj;
  fidl::unowned_ptr<uint32_t> ptr = fidl::unowned(&obj);
  EXPECT_EQ(ptr.get(), &obj);
}

TEST(Memory, VectorViewUnownedArray) {
  uint32_t obj[1] = {1};
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_EQ(vv.data(), std::data(obj));
}
TEST(Memory, VectorViewUnownedFidlArray) {
  fidl::Array<uint32_t, 1> obj = {1};
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_EQ(vv.data(), std::data(obj));
}
TEST(Memory, VectorViewUnownedStdVector) {
  std::vector<uint32_t> obj;
  obj.push_back(1);
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_EQ(vv.data(), std::data(obj));
}

TEST(Memory, VectorViewHeapCopyArray) {
  uint32_t obj[1] = {1};
  fidl::VectorView<uint32_t> vv = fidl::heap_copy_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}
TEST(Memory, VectorViewHeapCopyFidlArray) {
  fidl::Array<uint32_t, 1> obj = {1};
  fidl::VectorView<uint32_t> vv = fidl::heap_copy_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}
TEST(Memory, VectorViewHeapCopyStdVector) {
  std::vector<uint32_t> obj;
  obj.push_back(1);
  fidl::VectorView<uint32_t> vv = fidl::heap_copy_vec(obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}

TEST(Memory, VectorViewCopyArray) {
  fidl::BufferAllocator<2048> allocator;
  uint32_t obj[1] = {1};
  fidl::VectorView<uint32_t> vv = fidl::copy_vec(allocator, obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}
TEST(Memory, VectorViewCopyFidlArray) {
  fidl::BufferAllocator<2048> allocator;
  fidl::Array<uint32_t, 1> obj = {1};
  fidl::VectorView<uint32_t> vv = fidl::copy_vec(allocator, obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}
TEST(Memory, VectorViewCopyStdVector) {
  fidl::BufferAllocator<2048> allocator;
  std::vector<uint32_t> obj;
  obj.push_back(1);
  fidl::VectorView<uint32_t> vv = fidl::copy_vec(allocator, obj);
  EXPECT_EQ(vv.count(), std::size(obj));
  EXPECT_NE(vv.data(), std::data(obj));
  EXPECT_EQ(vv.data()[0], std::data(obj)[0]);
}
