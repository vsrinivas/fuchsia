// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/inline_any.h>

#include <cassert>

#include <zxtest/zxtest.h>

namespace {

//
// Basic type assertions and casting.
//

struct base {
  int a;
};

struct derived : base {
  derived(int a, int b) : base{a}, b{b} {}
  explicit derived(std::pair<int, int> pair) : derived(pair.first, pair.second) {}
  int b;
};

struct big_derived : base {
  big_derived(int a, int b, int c, int d) : base{a}, b{b}, c{c}, d{d} {}
  int b;
  int c;
  int d;
};

struct alignas(16) big_align : base {};

struct not_derived {
  int b;
};

__attribute__((noinline)) base get_base(int a) { return base{a}; }
__attribute__((noinline)) derived get_derived(int a, int b) { return derived{a, b}; }

// Turn this on to preview errors.
// #define DOES_NOT_COMPILE 1

TEST(InlineAny, Basic) {
  using any_type = fit::inline_any<base, sizeof(derived), alignof(derived)>;

  any_type any;
  EXPECT_TRUE(!any.has_value());
  EXPECT_TRUE(!any.is<base>());
  EXPECT_TRUE(!any.is<derived>());

#if 0 || DOES_NOT_COMPILE
  EXPECT_TRUE(!any.is<int>());
  EXPECT_TRUE(!any.is<not_derived>());
#endif

#if 0 || DOES_NOT_COMPILE
  any = big_derived{1, 2, 3, 4};
#endif

#if 0 || DOES_NOT_COMPILE
  any = big_align{1};
#endif

#ifdef __Fuchsia__
  ASSERT_DEATH([&] { any->a = 0; });
  ASSERT_DEATH([&] { (void)any->a; });
  ASSERT_DEATH([&] { any.as<derived>().b = 0; });
  ASSERT_DEATH([&] { (void)any.as<derived>().b; });
  ASSERT_DEATH([&] { any.visit([](base a) {}); });
  ASSERT_DEATH([&] { any.visit([](base& a) {}); });
  ASSERT_DEATH([&] { any.visit([](const base& a) {}); });
  ASSERT_DEATH([&] { any.visit([](base* a) {}); });
  ASSERT_DEATH([&] { any.visit([](const base* a) {}); });
  ASSERT_DEATH([&] { any.visit_as<derived>([](derived a) {}); });
  ASSERT_DEATH([&] { any.visit_as<derived>([](derived& a) {}); });
  ASSERT_DEATH([&] { any.visit_as<derived>([](const derived& a) {}); });
  ASSERT_DEATH([&] { any.visit_as<derived>([](derived* a) {}); });
  ASSERT_DEATH([&] { any.visit_as<derived>([](const derived& a) {}); });
#endif  // __Fuchsia__

  any = base{10};
  EXPECT_TRUE(any.has_value());
  EXPECT_TRUE(any.is<base>());
  EXPECT_TRUE(!any.is<derived>());

  EXPECT_EQ(10, any->a);
  EXPECT_EQ(10, any.as<base>().a);

  any = get_base(15);
  EXPECT_TRUE(any.has_value());
  EXPECT_TRUE(any.is<base>());
  EXPECT_TRUE(!any.is<derived>());

  EXPECT_EQ(15, any->a);
  EXPECT_EQ(15, any.as<base>().a);

  any = derived{10, 20};
  EXPECT_TRUE(any.has_value());
  EXPECT_TRUE(!any.is<base>());
  EXPECT_TRUE(any.is<derived>());

  EXPECT_EQ(10, any->a);
  EXPECT_EQ(10, any.as<derived>().a);
  EXPECT_EQ(20, any.as<derived>().b);

  any = get_derived(25, 30);
  EXPECT_TRUE(any.has_value());
  EXPECT_TRUE(!any.is<base>());
  EXPECT_TRUE(any.is<derived>());

  EXPECT_EQ(25, any->a);
  EXPECT_EQ(25, any.as<derived>().a);
  EXPECT_EQ(30, any.as<derived>().b);
}

TEST(InlineAny, MoveAndCopySameType) {
  using any_type = fit::inline_any<base, sizeof(derived), alignof(derived)>;

  any_type any1{get_derived(25, 30)};
  any_type any2{get_derived(1, 2)};
  any2 = std::move(any1);

  EXPECT_EQ(25, any2->a);
  EXPECT_EQ(25, any2.as<derived>().a);
  EXPECT_EQ(30, any2.as<derived>().b);

  any1 = get_derived(25, 30);
  any_type any3{any1};
  any2 = get_derived(1, 2);
  any_type any4{any2};
  any4 = any3;

  EXPECT_EQ(25, any4->a);
  EXPECT_EQ(25, any4.as<derived>().a);
  EXPECT_EQ(30, any4.as<derived>().b);
}

TEST(InlineAny, MoveAndCopyDifferentType) {
  using any_type = fit::inline_any<base, sizeof(derived), alignof(derived)>;

  any_type any{get_derived(25, 30)};
  any_type any2 = std::move(any);

  EXPECT_EQ(25, any2->a);
  EXPECT_EQ(25, any2.as<derived>().a);
  EXPECT_EQ(30, any2.as<derived>().b);

  any_type any3{get_base(15)};
  any3 = any2;

  EXPECT_EQ(25, any3->a);
  EXPECT_EQ(25, any3.as<derived>().a);
  EXPECT_EQ(30, any3.as<derived>().b);

  any_type any4{any2};

  EXPECT_EQ(25, any4->a);
  EXPECT_EQ(25, any4.as<derived>().a);
  EXPECT_EQ(30, any4.as<derived>().b);
}

TEST(InlineAny, Visit) {
  using any_type = fit::inline_any<base, sizeof(derived), alignof(derived)>;
  any_type any4{get_derived(25, 30)};

  EXPECT_EQ(25, any4.visit([](base v) { return v.a; }));

  EXPECT_EQ(25, any4.visit([](auto& v) { return v.a; }));
  EXPECT_EQ(25, any4.visit([](base& v) { return v.a; }));
  EXPECT_EQ(25, any4.visit([](const base& v) { return v.a; }));

  EXPECT_EQ(25, any4.visit([](auto* v) { return v->a; }));
  EXPECT_EQ(25, any4.visit([](base* v) { return v->a; }));
  EXPECT_EQ(25, any4.visit([](const base* v) { return v->a; }));

  EXPECT_EQ(25, any4.visit_as<derived>([](derived v) { return v.a; }));

  EXPECT_EQ(25, any4.visit_as<derived>([](auto& v) { return v.a; }));
  EXPECT_EQ(30, any4.visit_as<derived>([](derived& v) { return v.b; }));
  EXPECT_EQ(30, any4.visit_as<derived>([](const derived& v) { return v.b; }));

  EXPECT_EQ(25, any4.visit_as<derived>([](auto* v) { return v->a; }));
  EXPECT_EQ(30, any4.visit_as<derived>([](derived* v) { return v->b; }));
  EXPECT_EQ(30, any4.visit_as<derived>([](const derived* v) { return v->b; }));
}

//
// Calling type-erased interfaces through a v-table.
//

class dice_roll {
 public:
  dice_roll() = default;
  virtual ~dice_roll() = default;

  virtual int value() = 0;
};

class four : public dice_roll {
 public:
  explicit four(int* receiver = nullptr) : receiver_(receiver) {}
  ~four() override {
    if (receiver_)
      *receiver_ = 4;
  }
  four(four&& other) noexcept {
    receiver_ = other.receiver_;
    other.receiver_ = nullptr;
  }
  four& operator=(four&& other) noexcept {
    if (this != &other) {
      receiver_ = other.receiver_;
      other.receiver_ = nullptr;
    }
    return *this;
  }
  four(const four& other) noexcept { receiver_ = other.receiver_; }
  four& operator=(const four& other) {
    if (this != &other) {
      receiver_ = other.receiver_;
    }
    return *this;
  }
  int value() override { return 4; }

 private:
  int* receiver_ = nullptr;
};

class six : public dice_roll {
 public:
  explicit six(int* receiver = nullptr) : receiver_(receiver) {}
  ~six() override {
    if (receiver_)
      *receiver_ = 6;
  }
  six(six&& other) noexcept {
    receiver_ = other.receiver_;
    other.receiver_ = nullptr;
  }
  six& operator=(six&& other) noexcept {
    if (this != &other) {
      receiver_ = other.receiver_;
      other.receiver_ = nullptr;
    }
    return *this;
  }
  six(const six& other) noexcept { receiver_ = other.receiver_; }
  six& operator=(const six& other) {
    if (this != &other) {
      receiver_ = other.receiver_;
    }
    return *this;
  }
  int value() override { return 6; }

 private:
  int* receiver_ = nullptr;
};

using any_dice_roll = ::fit::inline_any<dice_roll, sizeof(four), alignof(four)>;

TEST(Any, DefaultConstruction) {
  any_dice_roll roll;
  EXPECT_FALSE(roll.has_value());

  static_assert(std::is_move_constructible<any_dice_roll>::value, "");
  static_assert(std::is_copy_constructible<any_dice_roll>::value, "");
}

TEST(InlineAny, Emplace) {
  any_dice_roll roll;
  roll.emplace<four>();
  EXPECT_TRUE(roll.has_value());
  EXPECT_EQ(4, roll->value());

  roll = six{};
  EXPECT_TRUE(roll.has_value());
  EXPECT_EQ(6, roll->value());
}

TEST(InlineAny, InPlaceTag) {
  int receiver = 0;
  {
    any_dice_roll roll{cpp17::in_place_type_t<four>{}, &receiver};
    EXPECT_EQ(0, receiver);
  }
  EXPECT_EQ(4, receiver);

  using any_type = fit::inline_any<base, sizeof(derived), alignof(derived)>;
  const any_type any3{std::in_place_type<derived>, std::pair{1, 2}};
  EXPECT_EQ(1, any3->a);
  EXPECT_EQ(1, any3.as<derived>().a);
  EXPECT_EQ(2, any3.as<derived>().b);
}

TEST(InlineAny, MoveNonTrivial) {
  int receiver = 0;
  any_dice_roll d4;
  d4 = four{&receiver};

  any_dice_roll roll;
  EXPECT_FALSE(roll.has_value());

  roll = std::move(d4);
  EXPECT_TRUE(roll.has_value());
  EXPECT_FALSE(d4.has_value());
  EXPECT_EQ(4, roll->value());
  EXPECT_EQ(0, receiver);

  roll.reset();
  EXPECT_EQ(4, receiver);
}

TEST(InlineAny, MoveAssignFromSpecificTypeNonTrivial) {
  int receiver = 0;
  four dice1{&receiver};
  int receiver2 = 0;
  four dice2{&receiver2};
  any_dice_roll roll{std::move(dice1)};
  EXPECT_EQ(0, receiver);
  // |roll| stores a |four|, move assign from another |four|.
  roll = std::move(dice2);
  EXPECT_EQ(0, receiver);
  EXPECT_EQ(0, receiver2);
  roll.reset();
  EXPECT_EQ(0, receiver);
  EXPECT_EQ(4, receiver2);
}

using non_movable_any_dice_roll = fit::pinned_inline_any<dice_roll, sizeof(four), alignof(four)>;

class non_movable_four : public four {
 public:
  using four::four;

  non_movable_four(const non_movable_four&) = delete;
  non_movable_four& operator=(const non_movable_four&) = delete;

  non_movable_four(non_movable_four&&) = delete;
  non_movable_four& operator=(non_movable_four&&) = delete;
};

TEST(PinnedInlineAny, DefaultConstruction) {
  non_movable_any_dice_roll roll;
  EXPECT_FALSE(roll.has_value());

  static_assert(!std::is_move_constructible<non_movable_any_dice_roll>::value, "");
  static_assert(!std::is_copy_constructible<non_movable_any_dice_roll>::value, "");
}

TEST(PinnedInlineAny, Emplace) {
  non_movable_any_dice_roll roll;
  roll.emplace<non_movable_four>();
  EXPECT_TRUE(roll.has_value());
  EXPECT_EQ(4, roll->value());

  roll = six{};
  EXPECT_TRUE(roll.has_value());
  EXPECT_EQ(6, roll->value());
}

TEST(PinnedInlineAny, InPlaceTag) {
  int receiver = 0;
  {
    non_movable_any_dice_roll roll{cpp17::in_place_type_t<non_movable_four>{}, &receiver};
    EXPECT_EQ(0, receiver);
  }
  EXPECT_EQ(4, receiver);
}

}  // namespace
