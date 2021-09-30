// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/cpp/fidl.h>
// [END include]

namespace {

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = fuchsia::examples::FileMode::READ | fuchsia::examples::FileMode::WRITE;
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);
  flags |= fuchsia::examples::FileMode::EXECUTE;
  ASSERT_EQ(flags, fuchsia::examples::FileModeMask);
}
// [END bits]

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia::examples::LocationType::MUSEUM), 1u);
}
// [END enums]

// [START structs]
TEST(FidlExamples, Structs) {
  fuchsia::examples::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  ASSERT_EQ(default_color.name, "red");

  fuchsia::examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}
// [END structs]

// [START unions]
TEST(FidlExamples, Unions) {
  auto int_val = fuchsia::examples::JsonValue::WithIntValue(1);
  ASSERT_EQ(int_val.Which(), fuchsia::examples::JsonValue::Tag::kIntValue);
  ASSERT_TRUE(int_val.is_int_value());

  auto str_val = fuchsia::examples::JsonValue::WithStringValue("1");
  ASSERT_EQ(str_val.Which(), fuchsia::examples::JsonValue::Tag::kStringValue);
  ASSERT_TRUE(str_val.is_string_value());

  fuchsia::examples::JsonValuePtr other_int_val = std::make_unique<fuchsia::examples::JsonValue>();
  other_int_val->set_int_value(5);
  ASSERT_EQ(other_int_val->int_value(), 5);
}
// [END unions]

// [START tables]
TEST(FidlExamples, Tables) {
  fuchsia::examples::User user;
  ASSERT_FALSE(user.has_age());
  user.set_age(100);
  *user.mutable_age() += 100;
  ASSERT_EQ(user.age(), 200);
  user.clear_age();
  ASSERT_TRUE(user.IsEmpty());
}
// [END tables]

}  // namespace
