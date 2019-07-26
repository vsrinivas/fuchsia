// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/result.h>
#include <unittest/unittest.h>

namespace {

struct Copyable {
  int data;
};

struct MoveOnly {
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly& operator=(MoveOnly&&) = default;

  int data;
};

bool states() {
  BEGIN_TEST;

  fit::result<> good = fit::ok();
  EXPECT_EQ(fit::result_state::ok, good.state());
  EXPECT_TRUE(good);
  EXPECT_TRUE(good.is_ok());
  EXPECT_FALSE(good.is_error());
  EXPECT_FALSE(good.is_pending());

  fit::result<> bad = fit::error();
  EXPECT_EQ(fit::result_state::error, bad.state());
  EXPECT_TRUE(bad);
  EXPECT_FALSE(bad.is_ok());
  EXPECT_TRUE(bad.is_error());
  EXPECT_FALSE(bad.is_pending());

  fit::result<> pending = fit::pending();
  EXPECT_EQ(fit::result_state::pending, pending.state());
  EXPECT_FALSE(pending);
  EXPECT_FALSE(pending.is_ok());
  EXPECT_FALSE(pending.is_error());
  EXPECT_TRUE(pending.is_pending());

  fit::result<> default_init;
  EXPECT_EQ(fit::result_state::pending, default_init.state());
  EXPECT_FALSE(default_init);
  EXPECT_FALSE(default_init.is_ok());
  EXPECT_FALSE(default_init.is_error());
  EXPECT_TRUE(default_init.is_pending());

  END_TEST;
}

bool void_value_and_error() {
  BEGIN_TEST;

  fit::result<> good = fit::ok();
  EXPECT_EQ(fit::result_state::ok, good.state());

  fit::result<> bad = fit::error();
  EXPECT_EQ(fit::result_state::error, bad.state());

  fit::result<> tmpcopy(good);
  EXPECT_EQ(fit::result_state::ok, tmpcopy.state());
  EXPECT_EQ(fit::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fit::result_state::error, tmpcopy.state());
  EXPECT_EQ(fit::result_state::error, bad.state());

  fit::result<> tmpmove(std::move(good));
  EXPECT_EQ(fit::result_state::ok, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fit::result_state::error, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, bad.state());

  fit::result<> tmpsrc = fit::ok();
  fit::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fit::error();
  fit::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_error_result;

  END_TEST;
}

bool copyable_value() {
  BEGIN_TEST;

  fit::result<Copyable> good = fit::ok<Copyable>({42});
  EXPECT_EQ(fit::result_state::ok, good.state());
  EXPECT_EQ(42, good.value().data);

  fit::result<Copyable> bad = fit::error();
  EXPECT_EQ(fit::result_state::error, bad.state());

  fit::result<Copyable> tmpcopy(good);
  EXPECT_EQ(fit::result_state::ok, tmpcopy.state());
  EXPECT_EQ(42, tmpcopy.value().data);
  EXPECT_EQ(fit::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fit::result_state::error, tmpcopy.state());
  EXPECT_EQ(fit::result_state::error, bad.state());

  fit::result<Copyable> tmpmove(std::move(good));
  EXPECT_EQ(fit::result_state::ok, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, good.state());
  EXPECT_EQ(42, tmpmove.value().data);
  tmpmove = std::move(bad);
  EXPECT_EQ(fit::result_state::error, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, bad.state());

  fit::result<Copyable> tmpsrc = fit::ok<Copyable>({42});
  Copyable taken_value = tmpsrc.take_value();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_value.data);
  tmpsrc = fit::ok<Copyable>({42});
  fit::ok_result<Copyable> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_ok_result.value.data);
  tmpsrc = fit::error();
  fit::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_error_result;

  END_TEST;
}

bool copyable_error() {
  BEGIN_TEST;

  fit::result<void, Copyable> good = fit::ok();
  EXPECT_EQ(fit::result_state::ok, good.state());

  fit::result<void, Copyable> bad = fit::error<Copyable>({42});
  EXPECT_EQ(fit::result_state::error, bad.state());
  EXPECT_EQ(42, bad.error().data);

  fit::result<void, Copyable> tmpcopy(good);
  EXPECT_EQ(fit::result_state::ok, tmpcopy.state());
  EXPECT_EQ(fit::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fit::result_state::error, tmpcopy.state());
  EXPECT_EQ(fit::result_state::error, bad.state());
  EXPECT_EQ(42, tmpcopy.error().data);

  fit::result<void, Copyable> tmpmove(std::move(good));
  EXPECT_EQ(fit::result_state::ok, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fit::result_state::error, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, bad.state());
  EXPECT_EQ(42, tmpmove.error().data);

  fit::result<void, Copyable> tmpsrc = fit::ok();
  fit::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fit::error<Copyable>({42});
  Copyable taken_error = tmpsrc.take_error();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error.data);
  tmpsrc = fit::error<Copyable>({42});
  fit::error_result<Copyable> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error_result.error.data);

  END_TEST;
}

bool moveonly_value() {
  BEGIN_TEST;

  fit::result<MoveOnly> good = fit::ok<MoveOnly>({42});
  EXPECT_EQ(fit::result_state::ok, good.state());
  EXPECT_EQ(42, good.value().data);

  fit::result<MoveOnly> bad = fit::error();
  EXPECT_EQ(fit::result_state::error, bad.state());

  fit::result<MoveOnly> tmpmove(std::move(good));
  EXPECT_EQ(fit::result_state::ok, tmpmove.state());
  EXPECT_EQ(42, tmpmove.value().data);
  EXPECT_EQ(fit::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fit::result_state::error, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, bad.state());

  fit::result<MoveOnly> tmpsrc = fit::ok<MoveOnly>({42});
  MoveOnly taken_value = tmpsrc.take_value();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_value.data);
  tmpsrc = fit::ok<MoveOnly>({42});
  fit::ok_result<MoveOnly> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_ok_result.value.data);
  tmpsrc = fit::error();
  fit::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_error_result;

  END_TEST;
}

bool moveonly_error() {
  BEGIN_TEST;

  fit::result<void, MoveOnly> good = fit::ok();
  EXPECT_EQ(fit::result_state::ok, good.state());

  fit::result<void, MoveOnly> bad = fit::error<MoveOnly>({42});
  EXPECT_EQ(fit::result_state::error, bad.state());
  EXPECT_EQ(42, bad.error().data);

  fit::result<void, MoveOnly> tmpmove(std::move(good));
  EXPECT_EQ(fit::result_state::ok, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fit::result_state::error, tmpmove.state());
  EXPECT_EQ(fit::result_state::pending, bad.state());
  EXPECT_EQ(42, tmpmove.error().data);

  fit::result<void, MoveOnly> tmpsrc = fit::ok();
  fit::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fit::error<MoveOnly>({42});
  MoveOnly taken_error = tmpsrc.take_error();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error.data);
  tmpsrc = fit::error<MoveOnly>({42});
  fit::error_result<MoveOnly> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fit::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error_result.error.data);

  END_TEST;
}

bool swapping() {
  BEGIN_TEST;

  fit::result<int, char> a, b, c;
  a = fit::ok(42);
  b = fit::error('x');

  a.swap(b);
  EXPECT_EQ('x', a.error());
  EXPECT_EQ(42, b.value());

  swap(b, c);
  EXPECT_EQ(42, c.value());
  EXPECT_TRUE(b.is_pending());

  swap(c, c);
  EXPECT_EQ(42, c.value());

  END_TEST;
}

// Test constexpr behavior.
namespace constexpr_test {
static_assert(fit::ok(1).value == 1, "");
static_assert(fit::error(1).error == 1, "");
static_assert(fit::result<>().state() == fit::result_state::pending, "");
static_assert(fit::result<>().is_pending(), "");
static_assert(!fit::result<>().is_ok(), "");
static_assert(!fit::result<>(), "");
static_assert(!fit::result<>().is_error(), "");
static_assert(fit::result<>(fit::pending()).state() == fit::result_state::pending, "");
static_assert(fit::result<>(fit::pending()).is_pending(), "");
static_assert(!fit::result<>(fit::pending()).is_ok(), "");
static_assert(!fit::result<>(fit::pending()), "");
static_assert(!fit::result<>(fit::pending()).is_error(), "");
static_assert(fit::result<>(fit::ok()).state() == fit::result_state::ok, "");
static_assert(!fit::result<>(fit::ok()).is_pending(), "");
static_assert(fit::result<>(fit::ok()).is_ok(), "");
static_assert(fit::result<>(fit::ok()), "");
static_assert(!fit::result<>(fit::ok()).is_error(), "");
static_assert(fit::result<int>(fit::ok(1)).state() == fit::result_state::ok, "");
static_assert(!fit::result<int>(fit::ok(1)).is_pending(), "");
static_assert(fit::result<int>(fit::ok(1)).is_ok(), "");
static_assert(fit::result<int>(fit::ok(1)), "");
static_assert(!fit::result<int>(fit::ok(1)).is_error(), "");
static_assert(fit::result<int>(fit::ok(1)).value() == 1, "");
static_assert(fit::result<>(fit::error()).state() == fit::result_state::error, "");
static_assert(!fit::result<>(fit::error()).is_pending(), "");
static_assert(!fit::result<>(fit::error()).is_ok(), "");
static_assert(fit::result<>(fit::error()), "");
static_assert(fit::result<>(fit::error()).is_error(), "");
static_assert(fit::result<void, int>(fit::error(1)).state() == fit::result_state::error, "");
static_assert(!fit::result<void, int>(fit::error(1)).is_pending(), "");
static_assert(!fit::result<void, int>(fit::error(1)).is_ok(), "");
static_assert(fit::result<void, int>(fit::error(1)), "");
static_assert(fit::result<void, int>(fit::error(1)).is_error(), "");
static_assert(fit::result<void, int>(fit::error(1)).error() == 1, "");
}  // namespace constexpr_test
}  // namespace

BEGIN_TEST_CASE(result_tests)
RUN_TEST(states)
RUN_TEST(void_value_and_error)
RUN_TEST(copyable_value)
RUN_TEST(copyable_error)
RUN_TEST(moveonly_value)
RUN_TEST(moveonly_error)
RUN_TEST(swapping)
END_TEST_CASE(result_tests)
