// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/inline_any.h>

#include <cassert>

#include <zxtest/zxtest.h>

// Test cases where the interface is not the first class in the object's
// inheritance chain.
namespace {

class base {
 public:
  base() = default;
  virtual ~base() = default;

  virtual void SetValue(std::string value) = 0;
  virtual std::string GetValue() = 0;

  int data = 0;
};

class some_other_base {
 public:
  some_other_base() = default;
  virtual ~some_other_base() = default;

  int other_data = 0;
};

class derived : public some_other_base, public base {
 public:
  derived() = default;

  void SetValue(std::string value) override { value_ = value; }

  std::string GetValue() override { return value_; }

 private:
  std::string value_;
};

TEST(InlineAny, MultipleInheritance) {
  // Check that we set up our inheritance chain as expected.
  derived d;
  EXPECT_NE(reinterpret_cast<void*>(static_cast<base*>(&d)), reinterpret_cast<void*>(&d));

  using any_type = fit::inline_any<base, 64>;
  any_type any;
  any = derived{};
  EXPECT_TRUE(any.has_value());
  any->SetValue("hello");
  EXPECT_EQ("hello", any->GetValue());

  any_type any2 = std::move(any);
  EXPECT_FALSE(any.has_value());
  EXPECT_TRUE(any2.has_value());
  EXPECT_EQ("hello", any2->GetValue());
}

}  // namespace
