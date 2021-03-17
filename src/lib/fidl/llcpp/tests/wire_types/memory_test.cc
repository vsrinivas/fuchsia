// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/memory.h>

#include <fbl/string.h>
#include <gtest/gtest.h>

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
