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
  // Bits implement bitwise operators such as |, ~, &, ^.
  auto flags = ~fuchsia_examples::FileMode::kRead & fuchsia_examples::FileMode::kExecute;
  flags = fuchsia_examples::FileMode::kRead | fuchsia_examples::FileMode::kWrite;

  // Bits may be explicitly casted to their underlying integer type.
  ASSERT_EQ(static_cast<uint16_t>(flags), 0b11);

  // They may also be explicitly constructed from an underlying type, but
  // this may result in invalid values for strict bits.
  flags = fuchsia_examples::FileMode(0b11);

  // A safer alternative is |TryFrom|, which constructs an instance of
  // |FileMode| only if underlying primitive does not contain any unknown
  // members that is not defined in the FIDL schema. Otherwise, returns
  // |std::nullopt|.
  std::optional<fuchsia_examples::FileMode> maybe_flags =
      fuchsia_examples::FileMode::TryFrom(0b1111);
  ASSERT_FALSE(maybe_flags.has_value());

  // Another alternative is |TruncatingUnknown| which clears any bits not
  // defined in the FIDL schema.
  fuchsia_examples::FileMode truncated_flags =
      fuchsia_examples::FileMode::TruncatingUnknown(0b1111);
  ASSERT_EQ(truncated_flags, fuchsia_examples::FileMode(0b111));

  // Bits implement bitwise-assignment.
  flags |= fuchsia_examples::FileMode::kExecute;

  // They also support equality and expose a |kMask| that is the
  // bitwise OR of all defined bit members.
  ASSERT_EQ(flags, fuchsia_examples::FileMode::kMask);

  // A flexible bits type additionally supports querying the unknown bits.
  fuchsia_examples::FlexibleFileMode flexible_flags = fuchsia_examples::FlexibleFileMode(0b1111);
  ASSERT_TRUE(flexible_flags.has_unknown_bits());
  ASSERT_EQ(static_cast<uint16_t>(flexible_flags.unknown_bits()), 0b1000);
}
// [END natural-bits]

// [START natural-enums]
TEST(NaturalTypes, Enums) {
  // Enums members are scoped constants under the enum type.
  fuchsia_examples::LocationType location = fuchsia_examples::LocationType::kAirport;

  // They may be explicitly casted to their underlying type.
  ASSERT_EQ(static_cast<uint32_t>(fuchsia_examples::LocationType::kMuseum), 1u);

  // Enums support switch case statements.
  // A strict enum can be switched exhaustively.
  (void)[=] {
    switch (location) {
      case fuchsia_examples::LocationType::kAirport:
        return 1;
      case fuchsia_examples::LocationType::kMuseum:
        return 2;
      case fuchsia_examples::LocationType::kRestaurant:
        return 3;
    }
  };

  // A flexible enum requires a `default:` case.
  fuchsia_examples::FlexibleLocationType flexible_location =
      fuchsia_examples::FlexibleLocationType::kAirport;
  (void)[=] {
    switch (flexible_location) {
      case fuchsia_examples::FlexibleLocationType::kAirport:
        return 1;
      case fuchsia_examples::FlexibleLocationType::kMuseum:
        return 2;
      case fuchsia_examples::FlexibleLocationType::kRestaurant:
        return 3;
      default:  // Removing this branch will fail to compile.
        return 4;
    }
  };

  // A flexible enum also supports asking if the current enum value was
  // not known in the FIDL schema.
  ASSERT_FALSE(flexible_location.IsUnknown());
}
// [END natural-enums]

// [START natural-structs]
TEST(NaturalTypes, Structs) {
  // Structs may be default constructed with fields set to default values,
  // provided that all fields are also default constructible.
  fuchsia_examples::Color default_color;
  ASSERT_EQ(default_color.id(), 0u);
  ASSERT_EQ(default_color.name(), "red");

  // They support constructing by supplying fields in a sequence.
  fuchsia_examples::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id(), 1u);

  // They also support a more readable syntax that names individual fields,
  // similar to C++ designated initialization. The double brace (`{{`) syntax
  // is necessary to workaround C++ limitations on aggregate initialization.
  fuchsia_examples::Color red{{.id = 2, .name = "red"}};
  ASSERT_EQ(red.id(), 2u);
  fuchsia_examples::Color designated_1 = {{.id = 1, .name = "designated"}};
  ASSERT_EQ(designated_1.id(), 1u);
  fuchsia_examples::Color designated_2{{.id = 2, .name = "designated"}};
  ASSERT_EQ(designated_2.id(), 2u);

  // Setters are simply accessors that return non-const references.
  fuchsia_examples::Color color;
  color.id() = 42;
  color.name() = "yellow";
  ASSERT_EQ(color.id(), 42u);
  ASSERT_EQ(color.name(), "yellow");

  // Equality is implemented for value types.
  ASSERT_EQ(color, fuchsia_examples::Color(42, "yellow"));

  // Copies and moves.
  fuchsia_examples::Color color_copy{color};
  ASSERT_EQ(color_copy.name(), "yellow");
  fuchsia_examples::Color color_moved{std::move(color)};
  ASSERT_EQ(color_moved.name(), "yellow");
  // The state of |color| is now unspecified.
}
// [END natural-structs]

// [START natural-unions]
TEST(NaturalTypes, Unions) {
  // Factory functions are used to construct natural union objects.
  // To construct a union whose active member is |int_value|, use |WithIntValue|.
  auto int_val = fuchsia_examples::JsonValue::WithIntValue(1);

  // |Which| obtains an enum corresponding to the active member, which may be
  // used in switch cases.
  ASSERT_EQ(int_val.Which(), fuchsia_examples::JsonValue::Tag::kIntValue);

  // When directly accessing a field, one must first check if the field is
  // active before dereferencing it.
  ASSERT_TRUE(int_val.int_value().has_value());
  ASSERT_TRUE(static_cast<bool>(int_val.int_value()));
  ASSERT_EQ(int_val.int_value().value(), 1);

  // Another example, this time activating the |string_value| member.
  auto str_val = fuchsia_examples::JsonValue::WithStringValue("1");
  ASSERT_EQ(str_val.Which(), fuchsia_examples::JsonValue::Tag::kStringValue);
  ASSERT_TRUE(str_val.string_value().has_value());

  // Unions are not default constructible, to avoid invalid states.
  static_assert(!std::is_default_constructible_v<fuchsia_examples::JsonValue>,
                "Unions cannot be default constructed");

  fuchsia_examples::JsonValue value = fuchsia_examples::JsonValue::WithStringValue("hello");
  ASSERT_FALSE(value.int_value());
  ASSERT_TRUE(value.string_value());

  // |value_or| returns a fallback if the corresponding member is not active.
  ASSERT_EQ(value.int_value().value_or(42), 42);

  // Setting a field causes that field to become the active member.
  value.int_value() = 2;
  ASSERT_TRUE(value.int_value());
  ASSERT_FALSE(value.string_value());

  // |take| invokes the move operation on the member if it is active.
  value.string_value() = "foo";
  std::optional<std::string> str = value.string_value().take();
  ASSERT_TRUE(str.has_value());
  ASSERT_EQ(str.value(), "foo");

  // Equality is implemented for value types.
  value.string_value() = "bar";
  ASSERT_EQ(value, fuchsia_examples::JsonValue::WithStringValue("bar"));

  // Copies and moves.
  fuchsia_examples::JsonValue value_copy{value};
  ASSERT_EQ(value.string_value().value(), "bar");
  fuchsia_examples::JsonValue value_moved{std::move(value)};
  ASSERT_EQ(value_moved.string_value().value(), "bar");

  // A flexible union additionally supports querying if the active member was
  // not defined in the FIDL schema.
  fuchsia_examples::FlexibleJsonValue flexible_value =
      fuchsia_examples::FlexibleJsonValue::WithIntValue(1);
  // If |flexible_value| was received from a peer with a different FIDL schema,
  // |Which| may return |kUnknown| if that peer sent a union with a member that
  // we do not understand. In this example |flexible_value| holds a known active
  // member.
  ASSERT_NE(flexible_value.Which(), fuchsia_examples::FlexibleJsonValue::Tag::kUnknown);
}
// [END natural-unions]

// [START natural-tables]
TEST(NaturalTypes, Tables) {
  // A default constructed table is empty. That is, every field is absent.
  fuchsia_examples::User user;
  ASSERT_TRUE(user.IsEmpty());

  // Each accessor returns a |std::optional<T>|, where |T| is the field type.
  ASSERT_FALSE(user.age().has_value());

  // To set fields, simply use the mutable accessors.
  user.age() = 100;
  user.age() = *user.age() + 100;
  ASSERT_EQ(user.age().value(), 200);

  // |value_or| returns a fallback if the corresponding field is absent.
  ASSERT_EQ(user.name().value_or("anonymous"), "anonymous");
  user.age().reset();
  ASSERT_TRUE(user.IsEmpty());

  // Similar to structs, tables support constructing by naming individual fields.
  // Fields that are omitted from the designated initialization syntax will be
  // absent from the table.
  user = {{.age = 100, .name = "foo"}};
  ASSERT_TRUE(user.age());
  ASSERT_TRUE(user.name());

  user = {{.age = 100}};
  ASSERT_TRUE(user.age());
  ASSERT_FALSE(user.name());

  // Equality is implemented for value types.
  ASSERT_EQ(user, fuchsia_examples::User{{.age = 100}});

  // Copies and moves.
  fuchsia_examples::User user_copy{user};
  ASSERT_EQ(*user.age(), 100);
  fuchsia_examples::User user_moved{std::move(user)};
  ASSERT_EQ(*user_moved.age(), 100);
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
