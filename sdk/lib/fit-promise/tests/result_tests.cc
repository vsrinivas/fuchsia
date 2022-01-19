// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/result.h>

#include <zxtest/zxtest.h>

namespace {

struct Copyable {
  int data;
};

struct MoveOnly {
  MoveOnly(int i) : data(i) {}

  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly& operator=(MoveOnly&&) = default;

  int data;
};

TEST(ResultTests, states) {
  fpromise::result<> good = fpromise::ok();
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  EXPECT_TRUE(good);
  EXPECT_TRUE(good.is_ok());
  EXPECT_FALSE(good.is_error());
  EXPECT_FALSE(good.is_pending());

  fpromise::result<> bad = fpromise::error();
  EXPECT_EQ(fpromise::result_state::error, bad.state());
  EXPECT_TRUE(bad);
  EXPECT_FALSE(bad.is_ok());
  EXPECT_TRUE(bad.is_error());
  EXPECT_FALSE(bad.is_pending());

  fpromise::result<> pending = fpromise::pending();
  EXPECT_EQ(fpromise::result_state::pending, pending.state());
  EXPECT_FALSE(pending);
  EXPECT_FALSE(pending.is_ok());
  EXPECT_FALSE(pending.is_error());
  EXPECT_TRUE(pending.is_pending());

  fpromise::result<> default_init;
  EXPECT_EQ(fpromise::result_state::pending, default_init.state());
  EXPECT_FALSE(default_init);
  EXPECT_FALSE(default_init.is_ok());
  EXPECT_FALSE(default_init.is_error());
  EXPECT_TRUE(default_init.is_pending());
}

TEST(ResultTests, void_value_and_error) {
  fpromise::result<> good = fpromise::ok();
  EXPECT_EQ(fpromise::result_state::ok, good.state());

  fpromise::result<> bad = fpromise::error();
  EXPECT_EQ(fpromise::result_state::error, bad.state());

  fpromise::result<> tmpcopy(good);
  EXPECT_EQ(fpromise::result_state::ok, tmpcopy.state());
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fpromise::result_state::error, tmpcopy.state());
  EXPECT_EQ(fpromise::result_state::error, bad.state());

  fpromise::result<> tmpmove(std::move(good));
  EXPECT_EQ(fpromise::result_state::ok, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fpromise::result_state::error, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, bad.state());

  fpromise::result<> tmpsrc = fpromise::ok();
  fpromise::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fpromise::error();
  fpromise::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_error_result;
}

TEST(ResultTests, copyable_value) {
  fpromise::result<Copyable> good = fpromise::ok<Copyable>({42});
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  EXPECT_EQ(42, good.value().data);

  fpromise::result<Copyable> bad = fpromise::error();
  EXPECT_EQ(fpromise::result_state::error, bad.state());

  fpromise::result<Copyable> tmpcopy(good);
  EXPECT_EQ(fpromise::result_state::ok, tmpcopy.state());
  EXPECT_EQ(42, tmpcopy.value().data);
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fpromise::result_state::error, tmpcopy.state());
  EXPECT_EQ(fpromise::result_state::error, bad.state());

  fpromise::result<Copyable> tmpmove(std::move(good));
  EXPECT_EQ(fpromise::result_state::ok, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, good.state());
  EXPECT_EQ(42, tmpmove.value().data);
  tmpmove = std::move(bad);
  EXPECT_EQ(fpromise::result_state::error, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, bad.state());

  fpromise::result<Copyable> tmpsrc = fpromise::ok<Copyable>({42});
  Copyable taken_value = tmpsrc.take_value();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_value.data);
  tmpsrc = fpromise::ok<Copyable>({42});
  fpromise::ok_result<Copyable> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_ok_result.value.data);
  tmpsrc = fpromise::error();
  fpromise::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_error_result;
}

TEST(ResultTests, copyable_error) {
  fpromise::result<void, Copyable> good = fpromise::ok();
  EXPECT_EQ(fpromise::result_state::ok, good.state());

  fpromise::result<void, Copyable> bad = fpromise::error<Copyable>({42});
  EXPECT_EQ(fpromise::result_state::error, bad.state());
  EXPECT_EQ(42, bad.error().data);

  fpromise::result<void, Copyable> tmpcopy(good);
  EXPECT_EQ(fpromise::result_state::ok, tmpcopy.state());
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  tmpcopy = bad;
  EXPECT_EQ(fpromise::result_state::error, tmpcopy.state());
  EXPECT_EQ(fpromise::result_state::error, bad.state());
  EXPECT_EQ(42, tmpcopy.error().data);

  fpromise::result<void, Copyable> tmpmove(std::move(good));
  EXPECT_EQ(fpromise::result_state::ok, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fpromise::result_state::error, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, bad.state());
  EXPECT_EQ(42, tmpmove.error().data);

  fpromise::result<void, Copyable> tmpsrc = fpromise::ok();
  fpromise::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fpromise::error<Copyable>({42});
  Copyable taken_error = tmpsrc.take_error();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error.data);
  tmpsrc = fpromise::error<Copyable>({42});
  fpromise::error_result<Copyable> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error_result.error.data);
}

TEST(ResultTests, moveonly_value) {
  fpromise::result<MoveOnly> good = fpromise::ok<MoveOnly>({42});
  EXPECT_EQ(fpromise::result_state::ok, good.state());
  EXPECT_EQ(42, good.value().data);

  fpromise::result<MoveOnly> bad = fpromise::error();
  EXPECT_EQ(fpromise::result_state::error, bad.state());

  fpromise::result<MoveOnly> tmpmove(std::move(good));
  EXPECT_EQ(fpromise::result_state::ok, tmpmove.state());
  EXPECT_EQ(42, tmpmove.value().data);
  EXPECT_EQ(fpromise::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fpromise::result_state::error, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, bad.state());

  fpromise::result<MoveOnly> tmpsrc = fpromise::ok<MoveOnly>({42});
  MoveOnly taken_value = tmpsrc.take_value();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_value.data);
  tmpsrc = fpromise::ok<MoveOnly>({42});
  fpromise::ok_result<MoveOnly> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_ok_result.value.data);
  tmpsrc = fpromise::error();
  fpromise::error_result<> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_error_result;
}

TEST(ResultTests, moveonly_error) {
  fpromise::result<void, MoveOnly> good = fpromise::ok();
  EXPECT_EQ(fpromise::result_state::ok, good.state());

  fpromise::result<void, MoveOnly> bad = fpromise::error<MoveOnly>({42});
  EXPECT_EQ(fpromise::result_state::error, bad.state());
  EXPECT_EQ(42, bad.error().data);

  fpromise::result<void, MoveOnly> tmpmove(std::move(good));
  EXPECT_EQ(fpromise::result_state::ok, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, good.state());
  tmpmove = std::move(bad);
  EXPECT_EQ(fpromise::result_state::error, tmpmove.state());
  EXPECT_EQ(fpromise::result_state::pending, bad.state());
  EXPECT_EQ(42, tmpmove.error().data);

  fpromise::result<void, MoveOnly> tmpsrc = fpromise::ok();
  fpromise::ok_result<> taken_ok_result = tmpsrc.take_ok_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  (void)taken_ok_result;
  tmpsrc = fpromise::error<MoveOnly>({42});
  MoveOnly taken_error = tmpsrc.take_error();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error.data);
  tmpsrc = fpromise::error<MoveOnly>({42});
  fpromise::error_result<MoveOnly> taken_error_result = tmpsrc.take_error_result();
  EXPECT_EQ(fpromise::result_state::pending, tmpsrc.state());
  EXPECT_EQ(42, taken_error_result.error.data);
}

TEST(ResultTests, swapping) {
  fpromise::result<int, char> a, b, c;
  a = fpromise::ok(42);
  b = fpromise::error('x');

  a.swap(b);
  EXPECT_EQ('x', a.error());
  EXPECT_EQ(42, b.value());

  swap(b, c);
  EXPECT_EQ(42, c.value());
  EXPECT_TRUE(b.is_pending());

  swap(c, c);
  EXPECT_EQ(42, c.value());
}

// Test constexpr behavior.
namespace constexpr_test {
static_assert(fpromise::ok(1).value == 1, "");
static_assert(fpromise::error(1).error == 1, "");
static_assert(fpromise::result<>().state() == fpromise::result_state::pending, "");
static_assert(fpromise::result<>().is_pending(), "");
static_assert(!fpromise::result<>().is_ok(), "");
static_assert(!fpromise::result<>(), "");
static_assert(!fpromise::result<>().is_error(), "");
static_assert(fpromise::result<>(fpromise::pending()).state() == fpromise::result_state::pending,
              "");
static_assert(fpromise::result<>(fpromise::pending()).is_pending(), "");
static_assert(!fpromise::result<>(fpromise::pending()).is_ok(), "");
static_assert(!fpromise::result<>(fpromise::pending()), "");
static_assert(!fpromise::result<>(fpromise::pending()).is_error(), "");
static_assert(fpromise::result<>(fpromise::ok()).state() == fpromise::result_state::ok, "");
static_assert(!fpromise::result<>(fpromise::ok()).is_pending(), "");
static_assert(fpromise::result<>(fpromise::ok()).is_ok(), "");
static_assert(fpromise::result<>(fpromise::ok()), "");
static_assert(!fpromise::result<>(fpromise::ok()).is_error(), "");
static_assert(fpromise::result<int>(fpromise::ok(1)).state() == fpromise::result_state::ok, "");
static_assert(!fpromise::result<int>(fpromise::ok(1)).is_pending(), "");
static_assert(fpromise::result<int>(fpromise::ok(1)).is_ok(), "");
static_assert(fpromise::result<int>(fpromise::ok(1)), "");
static_assert(!fpromise::result<int>(fpromise::ok(1)).is_error(), "");
static_assert(fpromise::result<int>(fpromise::ok(1)).value() == 1, "");
static_assert(fpromise::result<>(fpromise::error()).state() == fpromise::result_state::error, "");
static_assert(!fpromise::result<>(fpromise::error()).is_pending(), "");
static_assert(!fpromise::result<>(fpromise::error()).is_ok(), "");
static_assert(fpromise::result<>(fpromise::error()), "");
static_assert(fpromise::result<>(fpromise::error()).is_error(), "");
static_assert(fpromise::result<void, int>(fpromise::error(1)).state() ==
                  fpromise::result_state::error,
              "");
static_assert(!fpromise::result<void, int>(fpromise::error(1)).is_pending(), "");
static_assert(!fpromise::result<void, int>(fpromise::error(1)).is_ok(), "");
static_assert(fpromise::result<void, int>(fpromise::error(1)), "");
static_assert(fpromise::result<void, int>(fpromise::error(1)).is_error(), "");
static_assert(fpromise::result<void, int>(fpromise::error(1)).error() == 1, "");
}  // namespace constexpr_test
}  // namespace
