// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/llcpp/fidl.h>

#include <gtest/gtest.h>

namespace {

namespace fex = llcpp::fuchsia::examples;

TEST(FidlExamples, Bits) {
  auto flags = fex::FileMode::READ | fex::FileMode::WRITE | fex::FileMode::EXECUTE;
  ASSERT_EQ(flags, fex::FileMode::mask);
}

TEST(FidlExamples, Enums) { ASSERT_EQ(static_cast<uint32_t>(fex::LocationType::MUSEUM), 1u); }

TEST(FidlExamples, Structs) {
  fex::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  // Default values are currently not supported.
  ASSERT_TRUE(default_color.name.is_null());
  ASSERT_TRUE(default_color.name.empty());

  fex::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}

TEST(FidlExamples, Unions) {
  auto int_val = fex::JsonValue::WithIntValue(std::make_unique<int32_t>(1));
  auto str_val = fex::JsonValue::WithStringValue(std::make_unique<fidl::StringView>("1"));
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_TRUE(str_val.is_string_value());
}

TEST(FidlExamples, Tables) {
  fex::User user = fex::User::Builder(std::make_unique<fex::User::Frame>())
                       .set_age(std::make_unique<uint8_t>(30))
                       .set_name(std::make_unique<fidl::StringView>("jdoe"))
                       .build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}

TEST(AllocationExamples, UnionOrTableField) {
  // JsonValue is a FIDL union with field: "int32 int_value"
  fex::JsonValue val;
  val.set_int_value(std::make_unique<int32_t>(1));
}

TEST(AllocationExamples, VectorOrStringDataArrays) {
  fidl::VectorView<uint32_t> vec;
  vec.set_data(std::make_unique<uint32_t[]>(10));
}

TEST(AllocationExamples, VectorViewCopy) {
  std::vector<uint32_t> vec = {1, 2, 3};
  fidl::VectorView<uint32_t> vv = fidl::heap_copy_vec(vec);
}

TEST(AllocationExamples, StringViewCopy) {
  std::string_view str = "hello world";
  fidl::StringView sv = fidl::heap_copy_str(str);
}

TEST(AllocationExamples, AllocatorUnionOrTableField) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  fex::JsonValue val;
  val.set_int_value(allocator.make<int32_t>(1));
}

TEST(AllocationExamples, AllocatorVectorView) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  fidl::VectorView<uint32_t> vec;
  vec.set_data(allocator.make<uint32_t[]>(1));
}

TEST(AllocationExamples, AllocatorCopyVec) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  std::vector<uint32_t> vec;
  fidl::VectorView<uint32_t> vv = fidl::copy_vec(allocator, vec);
}

TEST(AllocationExamples, AllocatorCopyStr) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  std::string_view str = "hello world";
  fidl::StringView sv = fidl::copy_str(allocator, str);
}

TEST(AllocationExamples, UnownedPtr) {
  fex::JsonValue val;
  int32_t i = 1;
  val.set_int_value(fidl::unowned_ptr(&i));
}

TEST(AllocationExamples, UnownedVec) {
  std::vector<uint32_t> vec;
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(vec);
}

TEST(AllocationExamples, UnownedStr) {
  const char arr[] = {'h', 'e', 'l', 'l', 'o'};
  fidl::StringView sv = fidl::unowned_str(arr, 5);
}

TEST(AllocationExamples, StringViewLiteral) { fidl::StringView sv = "hello world"; }

}  // namespace
