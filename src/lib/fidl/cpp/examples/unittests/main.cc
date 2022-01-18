// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include <gtest/gtest.h>

// [START include]
#include <fidl/fuchsia.examples/cpp/fidl.h>
// [END include]

namespace {

// Verify that the wire bindings is available.
using WireFileMode = fuchsia_examples::wire::FileMode;
using ProtocolMarker = fuchsia_examples::Echo;

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = fuchsia_examples::FileMode::kRead | fuchsia_examples::FileMode::kWrite;
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);
  flags |= fuchsia_examples::FileMode::kExecute;
  ASSERT_EQ(flags, fuchsia_examples::FileMode::kMask);
}
// [END bits]
static_assert(std::is_same<fuchsia_examples::FileMode, fuchsia_examples::wire::FileMode>::value,
              "Natural types should be equivalent to Wire types");
static_assert(fuchsia_examples::FileMode::kMask == fuchsia_examples::wire::FileMode::kMask,
              "Natural types should be equivalent to Wire types");

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::LocationType::kMuseum), 1u);
}
// [END enums]
static_assert(
    std::is_same<fuchsia_examples::LocationType, fuchsia_examples::wire::LocationType>::value,
    "Natural types should be equivalent to Wire types");

// [START structs]
TEST(FidlExamples, Structs) {
  fuchsia_examples::Color default_color;
  ASSERT_EQ(default_color.id(), 0u);
  ASSERT_EQ(default_color.name(), "red");

  fuchsia_examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id(), 1u);

  fuchsia_examples::Color red{{.id = 2, .name = "red"}};
  ASSERT_EQ(red.id(), 2u);

  // Setters
  fuchsia_examples::Color color;
  color.id() = 42;
  color.name() = "yellow";
  ASSERT_EQ(color.id(), 42u);
  ASSERT_EQ(color.name(), "yellow");

  // Designated initializer support
  fuchsia_examples::Color designated_1 = {{.id = 1, .name = "designated"}};
  ASSERT_EQ(designated_1.id(), 1u);

  fuchsia_examples::Color designated_2{{.id = 2, .name = "designated"}};
  ASSERT_EQ(designated_2.id(), 2u);
}
// [END structs]

// [START unions]
TEST(FidlExamples, Unions) {
  auto int_val = fuchsia_examples::JsonValue::WithIntValue(1);
  ASSERT_EQ(int_val.Which(), fuchsia_examples::JsonValue::Tag::kIntValue);
  ASSERT_TRUE(int_val.int_value().has_value());

  auto str_val = fuchsia_examples::JsonValue::WithStringValue("1");
  ASSERT_EQ(str_val.Which(), fuchsia_examples::JsonValue::Tag::kStringValue);
  ASSERT_TRUE(str_val.string_value().has_value());

  fuchsia_examples::JsonValue value;
  ASSERT_FALSE(value.int_value());
  ASSERT_FALSE(value.string_value());
  value.string_value() = "hello";
  ASSERT_FALSE(value.int_value());
  ASSERT_TRUE(value.string_value());
  ASSERT_EQ(value.int_value().value_or(42), 42);
  value.int_value() = 2;
  ASSERT_TRUE(value.int_value());
  ASSERT_FALSE(value.string_value());
}
// [END unions]

// [START tables]
TEST(FidlExamples, Tables) {
  fuchsia_examples::User user;
  ASSERT_FALSE(user.age().has_value());
  user.age() = 100;
  user.age() = *user.age() + 100;
  ASSERT_EQ(user.age().value(), 200);
  ASSERT_EQ(user.name().value_or("anonymous"), "anonymous");
  user.age().reset();
  ASSERT_TRUE(user.IsEmpty());

  user = {{.age = 100, .name = "foo"}};
  ASSERT_TRUE(user.age());
  ASSERT_TRUE(user.name());
}
// [END tables]

}  // namespace
