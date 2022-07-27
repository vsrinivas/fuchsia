// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ async response tutorial.
// Head over there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/domain-objects
// ============================================================================

#include <type_traits>

#include <gtest/gtest.h>

// [START include]
#include <fidl/fuchsia.examples/cpp/fidl.h>
// [END include]

namespace {

//
// Examples of using the natural types.
//

// Verify that the wire types are available.
using WireFileMode = fuchsia_examples::wire::FileMode;
using ProtocolMarker = fuchsia_examples::Echo;

// [START natural-bits]
TEST(NaturalTypes, Bits) {
  auto flags = fuchsia_examples::FileMode::kRead | fuchsia_examples::FileMode::kWrite;
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);
  flags |= fuchsia_examples::FileMode::kExecute;
  ASSERT_EQ(flags, fuchsia_examples::FileMode::kMask);
}
// [END natural-bits]

// [START natural-enums]
TEST(NaturalTypes, Enums) {
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::LocationType::kMuseum), 1u);
}
// [END natural-enums]

// [START natural-structs]
TEST(NaturalTypes, Structs) {
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
// [END natural-structs]

// [START natural-unions]
TEST(NaturalTypes, Unions) {
  auto int_val = fuchsia_examples::JsonValue::WithIntValue(1);
  ASSERT_EQ(int_val.Which(), fuchsia_examples::JsonValue::Tag::kIntValue);
  ASSERT_TRUE(int_val.int_value().has_value());

  auto str_val = fuchsia_examples::JsonValue::WithStringValue("1");
  ASSERT_EQ(str_val.Which(), fuchsia_examples::JsonValue::Tag::kStringValue);
  ASSERT_TRUE(str_val.string_value().has_value());

  static_assert(!std::is_default_constructible_v<fuchsia_examples::JsonValue>,
                "Strict unions cannot be default constructed");
  fuchsia_examples::JsonValue value = fuchsia_examples::JsonValue::WithStringValue("hello");
  ASSERT_FALSE(value.int_value());
  ASSERT_TRUE(value.string_value());
  ASSERT_EQ(value.int_value().value_or(42), 42);
  value.int_value() = 2;
  ASSERT_TRUE(value.int_value());
  ASSERT_FALSE(value.string_value());
}
// [END natural-unions]

// [START natural-tables]
TEST(NaturalTypes, Tables) {
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
// [END natural-tables]

//
// Examples of using the wire types.
//

// [START wire-bits]
TEST(WireTypes, Bits) {
  static_assert(std::is_same<fuchsia_examples::FileMode, fuchsia_examples::wire::FileMode>::value,
                "natural bits should be equivalent to wire bits");
  static_assert(fuchsia_examples::FileMode::kMask == fuchsia_examples::wire::FileMode::kMask,
                "natural bits should be equivalent to wire bits");

  using fuchsia_examples::wire::FileMode;
  auto flags = FileMode::kRead | FileMode::kWrite | FileMode::kExecute;
  ASSERT_EQ(flags, FileMode::kMask);
}
// [END wire-bits]

// [START wire-enums]
TEST(WireTypes, Enums) {
  static_assert(
      std::is_same<fuchsia_examples::LocationType, fuchsia_examples::wire::LocationType>::value,
      "natural enums should be equivalent to wire enums");

  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::wire::LocationType::kMuseum), 1u);
}
// [END wire-enums]

// [START wire-structs]
TEST(WireTypes, Structs) {
  fuchsia_examples::wire::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  // Default values are currently not supported.
  ASSERT_TRUE(default_color.name.is_null());
  ASSERT_TRUE(default_color.name.empty());

  fuchsia_examples::wire::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
}
// [END wire-structs]

// [START wire-unions]
TEST(WireTypes, Unions) {
  fidl::Arena arena;
  auto int_val = fuchsia_examples::wire::JsonValue::WithIntValue(1);
  ASSERT_TRUE(int_val.is_int_value());
  ASSERT_EQ(1, int_val.int_value());

  auto str_val = fuchsia_examples::wire::JsonValue::WithStringValue(arena, "1");
  ASSERT_TRUE(str_val.is_string_value());
  ASSERT_EQ("1", str_val.string_value().get());
}
// [END wire-unions]

// [START wire-tables]
TEST(WireTypes, Tables) {
  fidl::Arena arena;
  // Construct a table creating a builder with an arena.
  auto builder = fuchsia_examples::wire::User::Builder(arena);
  // The |arena| passed to the builder will be used to allocate the table frame,
  // the inline portions of any fields and passed to the constructor of field
  // types.
  builder.name("jdoe");
  // The builder is turned into an actual instance by calling |Build()|.
  auto user = builder.Build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.name().get(), "jdoe");
}

TEST(WireTypes, TablesInlineSetter) {
  fidl::Arena arena;
  // Construct a table creating a builder with an arena.
  auto builder = fuchsia_examples::wire::User::Builder(arena);
  // Small values <= 4 bytes are inlined inside the frame of the table.
  builder.age(30);
  // The builder is turned into an actual instance by calling |Build()|.
  auto user = builder.Build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}

TEST(WireTypes, TablesDefaultConstructor) {
  fidl::Arena arena;
  // In some situations it could be difficult to provide an arena when
  // constructing tables. For example, here it is hard to provide constructor
  // arguments to 10 tables at once. When a table is default constructed, it
  // does not have an associated |fidl::WireTableFrame<T>|. A new table
  // instance should be built and assigned to the default constructed table.
  fidl::Array<fuchsia_examples::wire::User, 10> users;
  for (auto& user : users) {
    ASSERT_TRUE(user.IsEmpty());
    user = fuchsia_examples::wire::User::Builder(arena).age(30).name("jdoe").Build();
    ASSERT_FALSE(user.IsEmpty());
    ASSERT_EQ(user.age(), 30);
  }
  ASSERT_EQ(users[0].age(), 30);
}
// [END wire-tables]

// [START wire-external-object]
TEST(WireTypes, BorrowExternalObject) {
  fidl::StringView str("hello");
  // |object_view| is a view to the string view.
  fidl::ObjectView object_view = fidl::ObjectView<fidl::StringView>::FromExternal(&str);
  fuchsia_examples::wire::JsonValue val =
      fuchsia_examples::wire::JsonValue::WithStringValue(object_view);
  ASSERT_TRUE(val.is_string_value());
}
// [END wire-external-object]

// [START wire-external-vector]
TEST(WireTypes, BorrowExternalVector) {
  std::vector<uint32_t> vec = {1, 2, 3, 4};
  fidl::VectorView<uint32_t> vv = fidl::VectorView<uint32_t>::FromExternal(vec);
  ASSERT_EQ(vv.count(), 4UL);
}
// [END wire-external-vector]

// [START wire-external-string]
TEST(WireTypes, BorrowExternalString) {
  const char* string = "hello";
  fidl::StringView sv = fidl::StringView::FromExternal(string);
  ASSERT_EQ(sv.size(), 5UL);
}
// [END wire-external-string]

// [START wire-stringview-assign]
TEST(WireTypes, BorrowStringViewLiteral) {
  fidl::StringView sv1 = "hello world";
  fidl::StringView sv2("Hello");
  ASSERT_EQ(sv1.size(), 11UL);
  ASSERT_EQ(sv2.size(), 5UL);
}
// [END wire-stringview-assign]

}  // namespace
