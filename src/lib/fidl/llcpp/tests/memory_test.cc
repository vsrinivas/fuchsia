// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/buffer_allocator.h>
#include <lib/fidl/llcpp/memory.h>

#include <fbl/string.h>

#include "gtest/gtest.h"

TEST(Memory, TrackingPointerUnowned) {
  uint32_t obj;
  fidl::unowned_ptr_t<uint32_t> ptr = fidl::unowned_ptr(&obj);
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

TEST(Memory, StringViewUnownedStdString) {
  std::string str = "abcd";
  fidl::StringView sv = fidl::unowned_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_EQ(sv.data(), str.data());
}
TEST(Memory, StringViewUnownedFblString) {
  fbl::String str = "abcd";
  fidl::StringView sv = fidl::unowned_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_EQ(sv.data(), str.data());
}
TEST(Memory, StringViewUnownedStdStringView) {
  std::string_view str = "abcd";
  fidl::StringView sv = fidl::unowned_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_EQ(sv.data(), str.data());
}
TEST(Memory, StringViewUnownedCharPtrLength) {
  const char* str = "abcd";
  constexpr size_t len = 2;
  fidl::StringView sv = fidl::unowned_str(str, len);
  EXPECT_EQ(sv.size(), len);
  EXPECT_EQ(sv.data(), str);
}

TEST(Memory, StringViewHeapCopyStdString) {
  std::string str = "abcd";
  fidl::StringView sv = fidl::heap_copy_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewHeapCopyFblString) {
  fbl::String str = "abcd";
  fidl::StringView sv = fidl::heap_copy_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewHeapCopyStdStringView) {
  std::string_view str = "abcd";
  fidl::StringView sv = fidl::heap_copy_str(str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewHeapCopyCharPtrLength) {
  const char* str = "abcd";
  constexpr size_t len = 2;
  fidl::StringView sv = fidl::heap_copy_str(str, len);
  EXPECT_EQ(sv.size(), len);
  EXPECT_NE(sv.data(), str);
  for (size_t i = 0; i < len; i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}

TEST(Memory, StringViewCopyStdString) {
  fidl::BufferAllocator<2048> allocator;
  std::string str = "abcd";
  fidl::StringView sv = fidl::copy_str(allocator, str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewCopyFblString) {
  fidl::BufferAllocator<2048> allocator;
  fbl::String str = "abcd";
  fidl::StringView sv = fidl::copy_str(allocator, str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewCopyStdStringView) {
  fidl::BufferAllocator<2048> allocator;
  std::string_view str = "abcd";
  fidl::StringView sv = fidl::copy_str(allocator, str);
  EXPECT_EQ(sv.size(), str.size());
  EXPECT_NE(sv.data(), str.data());
  for (size_t i = 0; i < str.size(); i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
TEST(Memory, StringViewCopyCharPtrLength) {
  fidl::BufferAllocator<2048> allocator;
  const char* str = "abcd";
  constexpr size_t len = 2;
  fidl::StringView sv = fidl::copy_str(allocator, str, len);
  EXPECT_EQ(sv.size(), len);
  EXPECT_NE(sv.data(), str);
  for (size_t i = 0; i < len; i++) {
    EXPECT_EQ(sv[i], str[i]);
  }
}
