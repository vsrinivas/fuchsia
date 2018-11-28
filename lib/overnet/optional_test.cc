// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optional.h"
#include <memory>
#include "gtest/gtest.h"

namespace overnet {
namespace optional_test {

// Box allocates internally and has no move constructor
class Box {
 public:
  explicit Box(int n) : value_(std::make_unique<int>(n)) {}
  Box(const Box& other) : Box(*other.value_) {}
  Box& operator=(const Box& other) {
    value_ = std::make_unique<int>(*other.value_);
    return *this;
  }

  int operator*() const { return *value_; }

 private:
  std::unique_ptr<int> value_;
};

TEST(Optional, ShellGame) {
  Optional<int> x;
  EXPECT_FALSE(x.has_value());
  Optional<int> y(42);
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(*y, 42);
  Optional<int> z(123);
  EXPECT_TRUE(z);
  EXPECT_EQ(z.value(), 123);

  y.Swap(&z);
  EXPECT_TRUE(z.has_value());
  EXPECT_EQ(*z, 42);
  EXPECT_TRUE(y);
  EXPECT_EQ(y.value(), 123);

  x.Swap(&y);
  EXPECT_FALSE(y.has_value());
  EXPECT_TRUE(x);
  EXPECT_EQ(x.value(), 123);

  z.Swap(&y);
  EXPECT_FALSE(z.has_value());
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(*y, 42);

  y = z;
  EXPECT_FALSE(z.has_value());
  EXPECT_FALSE(y.has_value());
}

TEST(Optional, MoveGame) {
  Optional<std::unique_ptr<int>> x;
  EXPECT_FALSE(x.has_value());
  Optional<std::unique_ptr<int>> y(std::make_unique<int>(42));
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 42);
  Optional<std::unique_ptr<int>> z(std::make_unique<int>(123));
  EXPECT_TRUE(z);
  EXPECT_EQ(*z.value(), 123);

  y.Swap(&z);
  EXPECT_TRUE(z.has_value());
  EXPECT_EQ(**z, 42);
  EXPECT_TRUE(y);
  EXPECT_EQ(*y.value(), 123);

  x.Swap(&y);
  EXPECT_FALSE(y.has_value());
  EXPECT_TRUE(x);
  EXPECT_EQ(*x.value(), 123);

  z.Swap(&y);
  EXPECT_FALSE(z.has_value());
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 42);

  y = std::move(z);
  EXPECT_FALSE(y.has_value());
  y = std::move(x);
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 123);
}

TEST(Optional, MoveGameWithBox) {
  Optional<Box> x;
  EXPECT_FALSE(x.has_value());
  Optional<Box> y(Box(42));
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 42);
  Optional<Box> z(Box(123));
  EXPECT_TRUE(z);
  EXPECT_EQ(*z.value(), 123);

  y.Swap(&z);
  EXPECT_TRUE(z.has_value());
  EXPECT_EQ(**z, 42);
  EXPECT_TRUE(y);
  EXPECT_EQ(*y.value(), 123);

  x.Swap(&y);
  EXPECT_FALSE(y.has_value());
  EXPECT_TRUE(x);
  EXPECT_EQ(*x.value(), 123);

  z.Swap(&y);
  EXPECT_FALSE(z.has_value());
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 42);

  y = std::move(z);
  EXPECT_FALSE(y.has_value());
  y = std::move(x);
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 123);
}

TEST(Optional, MoveConstructor) {
  Optional<std::unique_ptr<int>> x(std::make_unique<int>(123));
  Optional<std::unique_ptr<int>> y(std::move(x));
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 123);
}

TEST(Optional, MoveConstructorWithBox) {
  Optional<Box> x(Box(123));
  Optional<Box> y(std::move(x));
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 123);
}

TEST(Optional, CopyConstructorWithBox) {
  Optional<Box> x(Box(123));
  Optional<Box> y(x);
  EXPECT_TRUE(y.has_value());
  EXPECT_EQ(**y, 123);
}

TEST(Optional, EqualityTest) {
  Optional<int> a;
  Optional<int> b(1);
  Optional<int> c(1);
  Optional<int> d(2);
  Optional<int> e;

  EXPECT_EQ(a, e);
  EXPECT_EQ(b, c);
  EXPECT_NE(a, b);
  EXPECT_NE(b, a);
  EXPECT_NE(c, d);
}

}  // namespace optional_test
}  // namespace overnet
