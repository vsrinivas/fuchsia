// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/cpp/fidl_v2.h>
// [END include]

namespace {

// Verify that the wire bindings is available.
using WireFileMode = fuchsia_examples::wire::FileMode;
using ProtocolMarker = fuchsia_examples::Echo;

// The following tests are adapted from the corresponding HLCPP tests at
// //examples/fidl/hlcpp/unittests/main.cc. The HLCPP namespaces were replaced with
// the underscore namespaces of the unified bindings.
// Another difference is that the JsonPtr tests were dropped - the "FooBarPtr" type
// aliases are deprecated in HLCPP (fxbug.dev/7342), and there isn't a strong reason
// to bring the FooBarPtr aliases into the unified bindings.

// [START bits]
TEST(FidlExamples, Bits) {
  auto flags = fuchsia_examples::FileMode::READ | fuchsia_examples::FileMode::WRITE;
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);
  flags |= fuchsia_examples::FileMode::EXECUTE;
  ASSERT_EQ(flags, fuchsia_examples::FileModeMask);
}
// [END bits]
static_assert(std::is_same<fuchsia_examples::FileMode, fuchsia::examples::FileMode>::value,
              "Natural types should be equivalent to HLCPP types");
static_assert(fuchsia_examples::FileModeMask == fuchsia::examples::FileModeMask,
              "Natural types should be equivalent to HLCPP types");

// [START enums]
TEST(FidlExamples, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::LocationType::MUSEUM), 1u);
}
// [END enums]
static_assert(std::is_same<fuchsia_examples::LocationType, fuchsia::examples::LocationType>::value,
              "Natural types should be equivalent to HLCPP types");

// [START structs]
TEST(FidlExamples, Structs) {
  fuchsia_examples::Color default_color;
  ASSERT_EQ(default_color.id(), 0u);
  ASSERT_EQ(default_color.name(), "red");

  fuchsia_examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id(), 1u);

  // Setters
  fuchsia_examples::Color color;
  color.set_id(42).set_name("yellow");
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
  ASSERT_TRUE(int_val.is_int_value());

  auto str_val = fuchsia_examples::JsonValue::WithStringValue("1");
  ASSERT_EQ(str_val.Which(), fuchsia_examples::JsonValue::Tag::kStringValue);
  ASSERT_TRUE(str_val.is_string_value());
}
// [END unions]
static_assert(std::is_same<fuchsia_examples::JsonValue, fuchsia::examples::JsonValue>::value,
              "Natural types should be equivalent to HLCPP types");

// [START tables]
TEST(FidlExamples, Tables) {
  fuchsia_examples::User user;
  ASSERT_FALSE(user.has_age());
  user.set_age(100);
  *user.mutable_age() += 100;
  ASSERT_EQ(user.age(), 200);
  user.clear_age();
  ASSERT_TRUE(user.IsEmpty());
}
// [END tables]
static_assert(std::is_same<fuchsia_examples::User, fuchsia::examples::User>::value,
              "Natural types should be equivalent to HLCPP types");

}  // namespace
