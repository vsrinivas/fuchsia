// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/any.h>

#include <string>

#include <zxtest/zxtest.h>

namespace {

class DiceRoll {
 public:
  DiceRoll() = default;
  virtual ~DiceRoll() = default;

  virtual int value() = 0;
};

class Four : public DiceRoll {
 public:
  explicit Four(int* receiver = nullptr) : receiver_(receiver) {}
  ~Four() override {
    if (receiver_)
      *receiver_ = 4;
  }
  Four(Four&& other) noexcept {
    receiver_ = other.receiver_;
    other.receiver_ = nullptr;
  }
  int value() override { return 4; }

 private:
  int* receiver_ = nullptr;
};

class Six : public DiceRoll {
 public:
  explicit Six(int* receiver = nullptr) : receiver_(receiver) {}
  ~Six() override {
    if (receiver_)
      *receiver_ = 6;
  }
  Six(Six&& other) noexcept {
    receiver_ = other.receiver_;
    other.receiver_ = nullptr;
  }
  int value() override { return 6; }

 private:
  int* receiver_ = nullptr;
};

using AnyDiceRoll = ::fidl::internal::Any<DiceRoll>;
using NonMovableAnyDiceRoll = ::fidl::internal::NonMovableAny<DiceRoll>;

}  // namespace

TEST(Any, DefaultConstruction) {
  AnyDiceRoll roll;
  EXPECT_FALSE(roll.is_valid());
}

TEST(Any, WrapObject) {
  AnyDiceRoll roll;
  roll.emplace<Four>();
  EXPECT_TRUE(roll.is_valid());
  EXPECT_EQ(4, roll->value());
  roll.emplace<Six>();
  EXPECT_TRUE(roll.is_valid());
  EXPECT_EQ(6, roll->value());
}

TEST(Any, Destruction) {
  int receiver = 0;
  {
    AnyDiceRoll roll;
    roll.emplace<Four>(&receiver);
    EXPECT_EQ(0, receiver);
  }
  EXPECT_EQ(4, receiver);
}

TEST(Any, Move) {
  int receiver = 0;
  AnyDiceRoll four;
  four.emplace<Four>(&receiver);

  AnyDiceRoll roll;
  EXPECT_FALSE(roll.is_valid());
  roll = std::move(four);
  EXPECT_TRUE(roll.is_valid());
  EXPECT_FALSE(four.is_valid());
  EXPECT_EQ(4, roll->value());
  EXPECT_EQ(0, receiver);
  roll = {};
  EXPECT_EQ(4, receiver);
}

// Test cases where the interface is not the first class in the object's
// inheritance chain.
namespace {

class Base {
 public:
  Base() = default;
  virtual ~Base() = default;

  virtual void SetValue(std::string value) = 0;
  virtual std::string GetValue() = 0;

  int data = 0;
};

class SomeOtherBase {
 public:
  SomeOtherBase() = default;
  virtual ~SomeOtherBase() = default;

  int other_data = 0;
};

class Derived : public SomeOtherBase, public Base {
 public:
  Derived() = default;

  void SetValue(std::string value) override { value_ = value; }

  std::string GetValue() override { return value_; }

 private:
  std::string value_;
};

}  // namespace

TEST(Any, MultipleInheritance) {
  // Check that we set up our inheritance chain as expected.
  Derived d;
  EXPECT_NE(reinterpret_cast<void*>(static_cast<Base*>(&d)), reinterpret_cast<void*>(&d));

  using Any = fidl::internal::Any<Base, 64>;
  Any any;
  any.emplace<Derived>();
  EXPECT_TRUE(any.is_valid());
  any->SetValue("hello");
  EXPECT_EQ("hello", any->GetValue());

  Any any2 = std::move(any);
  EXPECT_FALSE(any.is_valid());
  EXPECT_TRUE(any2.is_valid());
  EXPECT_EQ("hello", any2->GetValue());
}

TEST(NonMovableAny, DefaultConstruction) {
  NonMovableAnyDiceRoll roll;
  EXPECT_FALSE(roll.is_valid());
}

TEST(NonMovableAny, WrapObject) {
  NonMovableAnyDiceRoll roll;
  roll.emplace<Four>();
  EXPECT_TRUE(roll.is_valid());
  EXPECT_EQ(4, roll->value());
  roll.emplace<Six>();
  EXPECT_TRUE(roll.is_valid());
  EXPECT_EQ(6, roll->value());
}

TEST(NonMovableAny, Destruction) {
  int receiver = 0;
  {
    NonMovableAnyDiceRoll roll;
    roll.emplace<Four>(&receiver);
    EXPECT_EQ(0, receiver);
  }
  EXPECT_EQ(4, receiver);
}
