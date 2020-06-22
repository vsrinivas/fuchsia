// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/cpp/fidl.h>
// [END include]

namespace {

// [START tests]
TEST(FidlExamples, Bits) {
  auto flags = fuchsia::examples::FileMode::READ | fuchsia::examples::FileMode::WRITE;
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);
}

TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia::examples::LocationType::MUSEUM), 1u);
}

TEST(FidlExamples, Structs) {
  fuchsia::examples::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  ASSERT_EQ(default_color.name, "red");

  fuchsia::examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}

TEST(FidlExamples, Unions) {
  auto int_val = fuchsia::examples::JsonValue::WithIntValue(1);
  auto str_val = fuchsia::examples::JsonValue::WithStringValue("1");
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_TRUE(str_val.is_string_value());
}

TEST(FidlExamples, Tables) {
  fuchsia::examples::User user;
  ASSERT_TRUE(user.IsEmpty());
}
// [END tests]

}  // namespace
