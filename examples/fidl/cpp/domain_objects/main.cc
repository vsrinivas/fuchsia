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

  // They may also be casted to their underlying type without specifying the precise type.
  uint32_t strict_underlying = fidl::ToUnderlying(fuchsia_examples::LocationType::kMuseum);
  ASSERT_EQ(strict_underlying, 1u);

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
  // not known in the FIDL schema, or marked with `@unknown`.
  ASSERT_FALSE(flexible_location.IsUnknown());

  // Strict enums may be uninitialized. Their value will be undefined.
  fuchsia_examples::LocationType strict_location;
  (void)strict_location;

  // Flexible enums may be default initialized. They will either contain
  // the member marked with `@unknown` in the FIDL schema if exists,
  // or a compiler-reserved unknown value otherwise.
  fuchsia_examples::FlexibleLocationType default_flexible_location;
  ASSERT_TRUE(default_flexible_location.IsUnknown());
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

  // Setters take the value to be set as argument.
  fuchsia_examples::Color color;
  color.id(100);
  color.name("green");
  ASSERT_EQ(color.id(), 100u);
  ASSERT_EQ(color.name(), "green");

  // Setters may also be chained.
  color.id(42).name("yellow");
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

  // Setters take the value to be set as argument.
  // Setting a field causes that field to become the active member.
  value.int_value(2);
  ASSERT_TRUE(value.int_value());
  ASSERT_FALSE(value.string_value());

  // |take| invokes the move operation on the member if it is active.
  value.string_value("foo");
  std::optional<std::string> str = value.string_value().take();
  ASSERT_TRUE(str.has_value());
  ASSERT_EQ(str.value(), "foo");

  // Equality is implemented for value types.
  value.string_value("bar");
  ASSERT_EQ(value, fuchsia_examples::JsonValue::WithStringValue("bar"));

  // Copies and moves.
  fuchsia_examples::JsonValue value_copy{value};
  ASSERT_EQ(value.string_value().value(), "bar");
  fuchsia_examples::JsonValue value_moved{std::move(value)};
  ASSERT_EQ(value_moved.string_value().value(), "bar");

  // When switching over the tag from a flexible union, one must add a `default:`
  // case, to handle members not understood by the FIDL schema or to handle
  // newly added members in a source compatible way.
  fuchsia_examples::FlexibleJsonValue flexible_value =
      fuchsia_examples::FlexibleJsonValue::WithIntValue(1);
  switch (flexible_value.Which()) {
    case fuchsia_examples::FlexibleJsonValue::Tag::kIntValue:
      ASSERT_EQ(flexible_value.int_value().value(), 1);
      break;
    case fuchsia_examples::FlexibleJsonValue::Tag::kStringValue:
      FAIL() << "Unexpected tag. |flexible_value| was set to int";
      break;
    default:  // Removing this branch will fail to compile.
      break;
  }
}
// [END natural-unions]

// [START natural-tables]
TEST(NaturalTypes, Tables) {
  // A default constructed table is empty. That is, every field is absent.
  fuchsia_examples::User user;
  ASSERT_TRUE(user.IsEmpty());

  // Each accessor returns a |std::optional<T>|, where |T| is the field type.
  ASSERT_FALSE(user.age().has_value());

  // Setters take the value to be set as argument.
  user.age(100);
  user.age(*user.age() + 100);
  ASSERT_EQ(user.age().value(), 200);

  // Setters may also be chained.
  user.name("foo").age(30);
  ASSERT_EQ(user.name().value(), "foo");
  ASSERT_EQ(user.age().value(), 30);

  // Since each field is an |std::optional<T>|, they may also be cleared.
  user.name().reset();
  ASSERT_FALSE(user.name().has_value());

  // Assigning an |std::nullopt| also clears the field.
  user.name("bar");
  ASSERT_TRUE(user.name().has_value());
  user.name() = std::nullopt;
  ASSERT_FALSE(user.name().has_value());

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
  // Wire structs are simple C++ structs with all their member fields declared
  // public. One may invoke aggregate initialization:
  fuchsia_examples::wire::Color blue = {1, "blue"};
  ASSERT_EQ(blue.id, 1u);
  ASSERT_EQ(blue.name.get(), "blue");

  // ..or designated initialization.
  fuchsia_examples::wire::Color blue_designated = {.id = 1, .name = "blue"};
  ASSERT_EQ(blue_designated.id, 1u);
  ASSERT_EQ(blue_designated.name.get(), "blue");

  // A wire struct may be default constructed, but user-defined default values
  // are not supported.
  // Default-initializing a struct means all fields are zero-initialized.
  fuchsia_examples::wire::Color default_color;
  ASSERT_EQ(default_color.id, 0u);
  ASSERT_TRUE(default_color.name.is_null());
  ASSERT_TRUE(default_color.name.empty());

  // There are no getters/setters. One simply reads or mutates the member field.
  blue.id = 2;
  ASSERT_EQ(blue.id, 2u);

  // Here we demonstrate that wire structs do not own their out-of-line children.
  // Copying a struct will not copy their out-of-line children. Pointers are
  // simply aliased.
  {
    fuchsia_examples::wire::Color blue2 = blue;
    ASSERT_EQ(blue2.name.data(), blue.name.data());
  }
  // Similarly, destroying a wire struct object does not destroy out-of-line
  // children. Destroying |blue2| does not invalidate the string contents in |name|.
  ASSERT_EQ(blue.name.get(), "blue");
}
// [END wire-structs]

// [START wire-unions]
TEST(WireTypes, Unions) {
  // When the active member is larger than 4 bytes, it is stored out-of-line,
  // and the union will borrow the out-of-line content. The lifetimes can be
  // tricky to reason about, hence the FIDL runtime provides a |fidl::AnyArena|
  // interface for arena-based allocation of members. The built-in
  // implementation is |fidl::Arena|.
  //
  // Pass the arena as the first argument to |With...| factory functions, to
  // construct the member content on the arena, and have the union reference it.
  fidl::Arena arena;
  fuchsia_examples::wire::JsonValue str_union =
      fuchsia_examples::wire::JsonValue::WithStringValue(arena, "1");

  // |Which| obtains an enum corresponding to the active member, which may be
  // used in switch cases.
  ASSERT_EQ(str_union.Which(), fuchsia_examples::wire::JsonValue::Tag::kStringValue);

  // Before accessing the |string_value| member, one should check if the union
  // indeed currently holds this member, by querying |is_string_value|.
  // Accessing the wrong member will cause a panic.
  ASSERT_TRUE(str_union.is_string_value());
  ASSERT_EQ("1", str_union.string_value().get());

  // When the active member is smaller or equal to 4 bytes, such as an
  // |int32_t| here, the entire member is inlined into the union object.
  // In these cases, arena allocation is not necessary, and the union
  // object wholly owns the member.
  fuchsia_examples::wire::JsonValue int_union = fuchsia_examples::wire::JsonValue::WithIntValue(1);
  ASSERT_TRUE(int_union.is_int_value());
  ASSERT_EQ(1, int_union.int_value());

  // A default constructed wire union is invalid.
  // It must be initialized with a valid member before use.
  // One is not allowed to send invalid unions through FIDL client/server APIs.
  fuchsia_examples::wire::JsonValue default_union;
  ASSERT_TRUE(default_union.has_invalid_tag());
  default_union = fuchsia_examples::wire::JsonValue::WithStringValue(arena, "hello");
  ASSERT_FALSE(default_union.has_invalid_tag());
  ASSERT_TRUE(default_union.is_string_value());
  ASSERT_EQ(default_union.string_value().get(), "hello");

  // Optional unions are represented with |fidl::WireOptional|.
  fidl::WireOptional<fuchsia_examples::wire::JsonValue> optional_json;
  ASSERT_FALSE(optional_json.has_value());
  optional_json = fuchsia_examples::wire::JsonValue::WithIntValue(42);
  ASSERT_TRUE(optional_json.has_value());
  // |fidl::WireOptional| has a |std::optional|-like API.
  fuchsia_examples::wire::JsonValue& value = optional_json.value();
  ASSERT_TRUE(value.is_int_value());

  // When switching over the tag from a flexible union, one must add a `default:`
  // case, to handle members not understood by the FIDL schema or to handle
  // newly added members in a source compatible way.
  fuchsia_examples::wire::FlexibleJsonValue flexible_value =
      fuchsia_examples::wire::FlexibleJsonValue::WithIntValue(1);
  switch (flexible_value.Which()) {
    case fuchsia_examples::wire::FlexibleJsonValue::Tag::kIntValue:
      ASSERT_EQ(flexible_value.int_value(), 1);
      break;
    case fuchsia_examples::wire::FlexibleJsonValue::Tag::kStringValue:
      FAIL() << "Unexpected tag. |flexible_value| was set to int";
      break;
    default:  // Removing this branch will fail to compile.
      break;
  }
}
// [END wire-unions]

// [START wire-tables]
TEST(WireTypes, Tables) {
  fidl::Arena arena;
  // To construct a wire table, you need to first create a corresponding
  // |Builder| object, which borrows an arena. The |arena| will be used to
  // allocate the table frame, a bookkeeping structure for field presence.
  auto builder = fuchsia_examples::wire::User::Builder(arena);

  // To set a table field, call the member function with the same name on the
  // builder. The arguments will be forwarded to the field constructor, and the
  // field is allocated on the initial |arena|.
  builder.age(10);

  // Note that only the inline portion of the field is automatically placed in
  // the arena. The field itself may reference its own out-of-line content,
  // such as in the case of |name| whose type is |fidl::StringView|. |name|
  // will reference the "jdoe" literal, which lives in static program storage.
  builder.name("jdoe");

  // Call |Build| to finalize the table builder into a |User| table.
  // The builder is no longer needed after this point. |user| will continue to
  // reference objects allocated in the |arena|.
  fuchsia_examples::wire::User user = builder.Build();
  ASSERT_FALSE(user.IsEmpty());

  // Before accessing a field, one should check if it is present, by querying
  // |has_...|. Accessing an absent field will panic.
  ASSERT_TRUE(user.has_name());
  ASSERT_EQ(user.name().get(), "jdoe");

  // Setters may be chained, leading to a fluent syntax.
  user = fuchsia_examples::wire::User::Builder(arena).age(30).name("bob").Build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_TRUE(user.has_age());
  ASSERT_EQ(user.age(), 30);
  ASSERT_TRUE(user.has_name());
  ASSERT_EQ(user.name().get(), "bob");

  // A default constructed wire table is empty.
  // This is mostly useful to make requests or replies with empty tables.
  fuchsia_examples::wire::User defaulted_user;
  ASSERT_TRUE(defaulted_user.IsEmpty());

  // In some situations it could be difficult to provide an arena when
  // constructing tables. For example, here it is hard to provide constructor
  // arguments to 10 tables at once. Because a default constructed wire table is
  // empty, a new table instance should be built and assigned in its place.
  fidl::Array<fuchsia_examples::wire::User, 10> users;
  for (auto& user : users) {
    ASSERT_TRUE(user.IsEmpty());
    user = fuchsia_examples::wire::User::Builder(arena).age(30).Build();
    ASSERT_FALSE(user.IsEmpty());
    ASSERT_EQ(user.age(), 30);
  }
  ASSERT_EQ(users[0].age(), 30);

  // Finally, tables support checking if it was received with unknown fields.
  // A table created by ourselves will never have unknown fields.
  ASSERT_FALSE(user.HasUnknownData());
}
// [END wire-tables]

//
// Examples of converting between wire and natural types.
//

// [START natural-to-wire]
TEST(Conversion, NaturalToWire) {
  // Let's start with a natural table.
  fuchsia_examples::User user{{.age = 100, .name = "foo"}};

  // To convert it to its corresponding wire domain object, we need a
  // |fidl::AnyArena| implementation to allocate the storage, here an |arena|.
  fidl::Arena arena;

  // Call |fidl::ToWire| with the arena and the natural domain object.
  // All out-of-line fields will live on the |arena|.
  fuchsia_examples::wire::User wire_user = fidl::ToWire(arena, user);
  ASSERT_TRUE(wire_user.has_age());
  ASSERT_EQ(wire_user.age(), 100);
  ASSERT_TRUE(wire_user.has_name());
  ASSERT_EQ(wire_user.name().get(), "foo");
}
// [END natural-to-wire]

// [START wire-to-natural]
TEST(Conversion, WireToNatural) {
  fidl::Arena arena;

  // Let's start with a wire table.
  fuchsia_examples::wire::User wire_user =
      fuchsia_examples::wire::User::Builder(arena).age(30).name("bob").Build();

  // Call |fidl::ToNatural| with the wire domain object.
  // All child fields will be owned by |user|.
  fuchsia_examples::User user = fidl::ToNatural(wire_user);
  ASSERT_TRUE(user.age().has_value());
  ASSERT_EQ(user.age().value(), 30);
  ASSERT_TRUE(user.name().has_value());
  ASSERT_EQ(user.name().value(), "bob");
}
// [END wire-to-natural]

}  // namespace
