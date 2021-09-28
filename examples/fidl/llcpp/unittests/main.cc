// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fidl/fuchsia.examples/cpp/wire.h>
// [END include]

namespace {

using fuchsia_examples::wire::FileMode;

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = FileMode::kRead | FileMode::kWrite | FileMode::kExecute;
  ASSERT_EQ(flags, FileMode::kMask);
}
// [END bits]

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::wire::LocationType::kMuseum), 1u);
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
  fidl::Arena allocator;
  auto int_val = fuchsia_examples::wire::JsonValue::WithIntValue(1);
  auto str_val = fuchsia_examples::wire::JsonValue::WithStringValue(allocator, "1");
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_TRUE(str_val.is_string_value());
}
// [END unions]

// [START tables]
TEST(FidlExamples, Tables) {
  fidl::Arena allocator;
  fuchsia_examples::wire::User user(allocator);
  user.set_age(allocator, 30);
  user.set_name(allocator, allocator, "jdoe");
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}
// [END tables]

// [START external-object]
TEST(AllocationExamples, ExternalObject) {
  fuchsia_examples::wire::JsonValue val;
  val.set_int_value(1);
}
// [END external-object]

// [START external-vector]
TEST(AllocationExamples, ExternalVector) {
  std::vector<uint32_t> vec = {1, 2, 3, 4};
  fidl::VectorView<uint32_t> vv = fidl::VectorView<uint32_t>::FromExternal(vec);
  ASSERT_EQ(vv.count(), 4UL);
}
// [END external-vector]

// [START external-string]
TEST(AllocationExamples, ExternalString) {
  const char* string = "hello";
  fidl::StringView sv = fidl::StringView::FromExternal(string);
  ASSERT_EQ(sv.size(), 5UL);
}
// [END external-string]

TEST(AllocationExamples, StringViewLiteral) {
  // [START stringview-assign]
  fidl::StringView sv1 = "hello world";
  fidl::StringView sv2("Hello");
  ASSERT_EQ(sv1.size(), 11UL);
  ASSERT_EQ(sv2.size(), 5UL);
  // [END stringview-assign]
}

}  // namespace
