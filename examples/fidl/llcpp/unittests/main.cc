// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/llcpp/fidl.h>
// [END include]

namespace {

using fuchsia_examples::wire::FileMode;

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = FileMode::READ | FileMode::WRITE | FileMode::EXECUTE;
  ASSERT_EQ(flags, FileMode::kMask);
}
// [END bits]

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::wire::LocationType::MUSEUM), 1u);
}
// [END enums]

// [START structs]
TEST(FidlExamples, Structs) {
  fuchsia_examples::wire::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  // Default values are currently not supported.
  ASSERT_TRUE(default_color.name.is_null());
  ASSERT_TRUE(default_color.name.empty());

  fuchsia_examples::wire::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}
// [END structs]

// [START unions]
TEST(FidlExamples, Unions) {
  fidl::FidlAllocator allocator;
  auto int_val = fuchsia_examples::wire::JsonValue::WithIntValue(allocator, 1);
  auto str_val = fuchsia_examples::wire::JsonValue::WithStringValue(allocator, "1");
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_TRUE(str_val.is_string_value());
}
// [END unions]

// [START tables]
TEST(FidlExamples, Tables) {
  fidl::FidlAllocator allocator;
  fuchsia_examples::wire::User user(allocator);
  user.set_age(allocator, 30);
  user.set_name(allocator, allocator, "jdoe");
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}
// [END tables]

// [START unowned-ptr]
TEST(AllocationExamples, UnownedPtr) {
  fuchsia_examples::wire::JsonValue val;
  int32_t i = 1;
  val.set_int_value(fidl::unowned_ptr(&i));
}
// [END unowned-ptr]

// [START unowned-vec]
TEST(AllocationExamples, UnownedVec) {
  std::vector<uint32_t> vec = { 1, 2, 3, 4 };
  fidl::VectorView<uint32_t> vv = fidl::unowned_vec(vec);
  ASSERT_EQ(vv.count(), 4UL);
}
// [END unowned-vec]

// [START unowned-str]
TEST(AllocationExamples, UnownedStr) {
  const char arr[] = {'h', 'e', 'l', 'l', 'o'};
  fidl::StringView sv = fidl::unowned_str(arr, 5);
  ASSERT_EQ(sv.size(), 5UL);
}
// [END unowned-str]

TEST(AllocationExamples, StringViewLiteral) {
  // [START stringview-assign]
  fidl::StringView sv = "hello world";
  ASSERT_EQ(sv.size(), 11UL);
  // [END stringview-assign]
}

}  // namespace
