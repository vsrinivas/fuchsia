// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/llcpp/fidl.h>
// [END include]

namespace {

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = llcpp::fuchsia::examples::FileMode::READ |
               llcpp::fuchsia::examples::FileMode::WRITE |
               llcpp::fuchsia::examples::FileMode::EXECUTE;
  ASSERT_EQ(flags, llcpp::fuchsia::examples::FileMode::mask);
}
// [END bits]

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(llcpp::fuchsia::examples::LocationType::MUSEUM), 1u);
}
// [END enums]

// [START structs]
TEST(FidlExamples, Structs) {
  llcpp::fuchsia::examples::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  // Default values are currently not supported.
  ASSERT_TRUE(default_color.name.is_null());
  ASSERT_TRUE(default_color.name.empty());

  llcpp::fuchsia::examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}
// [END structs]

// [START unions]
TEST(FidlExamples, Unions) {
  auto int_val = llcpp::fuchsia::examples::JsonValue::WithIntValue(std::make_unique<int32_t>(1));
  auto str_val =
      llcpp::fuchsia::examples::JsonValue::WithStringValue(std::make_unique<fidl::StringView>("1"));
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_TRUE(str_val.is_string_value());
}
// [END unions]

// [START tables]
TEST(FidlExamples, Tables) {
  llcpp::fuchsia::examples::User user =
      llcpp::fuchsia::examples::User::Builder(
          std::make_unique<llcpp::fuchsia::examples::User::Frame>())
          .set_age(std::make_unique<uint8_t>(30))
          .set_name(std::make_unique<fidl::StringView>("jdoe"))
          .build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}
// [END tables]

// [START heap-field]
TEST(AllocationExamples, UnionOrTableField) {
  // JsonValue is a FIDL union with field: "int32 int_value"
  llcpp::fuchsia::examples::JsonValue val;
  val.set_int_value(std::make_unique<int32_t>(1));
}
// [END heap-field]

// [START heap-vec]
TEST(AllocationExamples, VectorOrStringDataArrays) {
  fidl::VectorView<uint32_t> vec;
  vec.set_data(std::make_unique<uint32_t[]>(10));
}
// [END heap-vec]

// [START heap-copy-vec]
TEST(AllocationExamples, VectorViewCopy) {
  std::vector<uint32_t> vec = {1, 2, 3};
  fidl::VectorView<uint32_t> vv = fidl::heap_copy_vec(vec);
}
// [END heap-copy-vec]

// [START heap-copy-str]
TEST(AllocationExamples, StringViewCopy) {
  std::string_view str = "hello world";
  fidl::StringView sv = fidl::heap_copy_str(str);
}
// [END heap-copy-str]

// [START allocator-field]
TEST(AllocationExamples, AllocatorUnionOrTableField) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  llcpp::fuchsia::examples::JsonValue val;
  val.set_int_value(allocator.make<int32_t>(1));
}
// [END allocator-field]

// [START allocator-vec]
TEST(AllocationExamples, AllocatorVectorView) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  fidl::VectorView<uint32_t> vec;
  vec.set_data(allocator.make<uint32_t[]>(1));
}
// [END allocator-vec]

// [START copy-vec]
TEST(AllocationExamples, AllocatorCopyVec) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  std::vector<uint32_t> vec;
  fidl::VectorView<uint32_t> vv = fidl::copy_vec(allocator, vec);
}
// [END copy-vec]

// [START copy-str]
TEST(AllocationExamples, AllocatorCopyStr) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  std::string_view str = "hello world";
  fidl::StringView sv = fidl::copy_str(allocator, str);
}
// [END copy-str]

// [START unowned-ptr]
TEST(AllocationExamples, UnownedPtr) {
  llcpp::fuchsia::examples::JsonValue val;
  int32_t i = 1;
  val.set_int_value(fidl::unowned_ptr(&i));
}
// [END unowned-ptr]

// [START unowned-vec]
TEST(AllocationExamples, UnownedVec) {
  std::vector<uint32_t> vec;
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(vec);
}
// [END unowned-vec]

// [START unowned-str]
TEST(AllocationExamples, UnownedStr) {
  const char arr[] = {'h', 'e', 'l', 'l', 'o'};
  fidl::StringView sv = fidl::unowned_str(arr, 5);
}
// [END unowned-str]

TEST(AllocationExamples, StringViewLiteral) {
  // [START stringview-assign]
  fidl::StringView sv = "hello world";
  // [END stringview-assign]
}

}  // namespace
