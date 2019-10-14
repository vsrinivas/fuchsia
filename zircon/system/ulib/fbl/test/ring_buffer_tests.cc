// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fbl/ring_buffer.h>
#include <zxtest/zxtest.h>

namespace {

enum class AddBehavior { kTestPush, kTestEmplace };

template <AddBehavior behavior>
void pod_test_helper() {
  constexpr uint32_t kBuffSize = 10;
  fbl::RingBuffer<uint8_t, kBuffSize> buffer;
  ASSERT_EQ(buffer.size(), 0);
  ASSERT_TRUE(buffer.empty());

  // Fill the buffer to capacity.
  for (uint8_t i = 0; i < static_cast<uint8_t>(kBuffSize); i++) {
    if constexpr (behavior == AddBehavior::kTestPush) {
      buffer.push(i);
    } else {
      buffer.emplace(i);
    }
    ASSERT_EQ(buffer.front(), 0);
    ASSERT_EQ(buffer.back(), i);
  }

  ASSERT_TRUE(buffer.full());
  ASSERT_EQ(buffer.front(), 0);

  for (uint8_t i = 0; i < static_cast<uint8_t>(kBuffSize); i++) {
    EXPECT_EQ(buffer.front(), i);
    ASSERT_EQ(buffer.back(), kBuffSize - 1);
    buffer.pop();
  }

  ASSERT_TRUE(buffer.empty());

  // Double check one more push now to check wrap-around.
  if constexpr (behavior == AddBehavior::kTestPush) {
    buffer.push(11);
  } else {
    buffer.emplace(static_cast<uint8_t>(11));
  }
  EXPECT_EQ(buffer.front(), 11);
}

TEST(RingBufferTest, PodPush) {
  auto fn = pod_test_helper<AddBehavior::kTestPush>;
  ASSERT_NO_FAILURES(fn());
}

TEST(RingBufferTest, PodEmplace) {
  auto fn = pod_test_helper<AddBehavior::kTestEmplace>;
  ASSERT_NO_FAILURES(fn());
}

TEST(RingBufferTest, EmptyAsserts) {
  if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    constexpr uint32_t kBuffSize = 10;
    fbl::RingBuffer<uint8_t, kBuffSize> buffer;

    ASSERT_DEATH([&buffer]() { buffer.pop(); },
                 "Assert should have fired after popping an empty buffer\n");

    ASSERT_DEATH([&buffer]() { buffer.front(); },
                 "Assert should have fired after calling front on an empty buffer\n");

    ASSERT_DEATH([&buffer]() { buffer.back(); },
                 "Assert should have fired after calling back on an empty buffer\n");
  }
}

TEST(RingBufferTest, FullAsserts) {
  if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    constexpr uint32_t kBuffSize = 10;
    fbl::RingBuffer<int, kBuffSize> buffer;

    // Fill the buffer to capacity.
    for (uint8_t i = 0; i < static_cast<uint8_t>(kBuffSize); i++) {
      buffer.push(i);
    }

    ASSERT_DEATH([&buffer]() { buffer.push(5); },
                 "Assert should have fired after pushing to a full buffer\n");

    ASSERT_DEATH([&buffer]() { buffer.emplace(5); },
                 "Assert should have fired after emplacing to a full buffer\n");
  }
}

TEST(RingBufferTest, MoveOnly) {
  constexpr uint32_t kBuffSize = 10;
  fbl::RingBuffer<std::unique_ptr<uint8_t>, kBuffSize> buffer;
  uint8_t data_value = 1;

  // Test pushing a move-only type.
  auto data = std::make_unique<uint8_t>(data_value);
  buffer.push(std::move(data));

  const std::unique_ptr<uint8_t>& data_ref = buffer.front();
  ASSERT_EQ(*data_ref, data_value);

  buffer.pop();
  data_value++;

  // Test emplace-ing a move-only type.
  buffer.emplace(new uint8_t(data_value));
  ASSERT_EQ(*buffer.front(), data_value);
  buffer.pop();
  data_value++;

  buffer.emplace(std::make_unique<uint8_t>(data_value));
  ASSERT_EQ(*buffer.front(), data_value);
  buffer.pop();
  data_value++;
}

class TestObj {
 public:
  explicit TestObj(int a) : a_(a) { constructed_++; }
  TestObj(TestObj&& obj) {
    *this = std::move(obj);
    obj.valid_obj_ = false;
  }
  ~TestObj() {
    if (valid_obj_) {
      destructed_++;
    }
  }

  TestObj& operator=(TestObj&& other) {
    valid_obj_ = other.valid_obj_;
    a_ = other.a_;

    other.valid_obj_ = false;
    other.a_ = 0;
    return *this;
  }

  int GetA() const { return a_; }

  static uint32_t ConstructCount() { return constructed_; }
  static uint32_t DestructCount() { return destructed_; }
  static void ResetConstructCount() { constructed_ = 0; }
  static void ResetDestructCount() { destructed_ = 0; }

 private:
  static uint32_t constructed_;
  static uint32_t destructed_;

  // Tracks valid objects so we don't count destructors that are called on objects that have
  // already been moved.
  bool valid_obj_ = true;
  int a_ = 0;
};

uint32_t TestObj::constructed_ = 0;
uint32_t TestObj::destructed_ = 0;

TEST(RingBufferTest, NoDefaultConstructor) {
  constexpr uint32_t kBuffSize = 10;
  fbl::RingBuffer<TestObj, kBuffSize> buffer;
  buffer.push(TestObj(1));
  buffer.emplace(2);

  ASSERT_EQ(buffer.front().GetA(), 1);
  ASSERT_EQ(buffer.back().GetA(), 2);
}

TEST(RingBufferTest, ConstructDestructMatch) {
  TestObj::ResetDestructCount();
  TestObj::ResetConstructCount();

  ASSERT_EQ(TestObj::ConstructCount(), 0);
  ASSERT_EQ(TestObj::DestructCount(), 0);

  {
    constexpr uint32_t kBuffSize = 10;
    fbl::RingBuffer<TestObj, kBuffSize> buffer;

    // Push and pop an object and assert the constructors and destructors are called.
    buffer.push(TestObj(1));
    EXPECT_EQ(TestObj::ConstructCount(), 1);
    EXPECT_EQ(TestObj::DestructCount(), 0);

    buffer.pop();
    EXPECT_EQ(TestObj::ConstructCount(), 1);
    EXPECT_EQ(TestObj::DestructCount(), 1);

    // Put two more objects on and call clear().
    buffer.emplace(2);
    EXPECT_EQ(TestObj::ConstructCount(), 2);
    EXPECT_EQ(TestObj::DestructCount(), 1);

    buffer.push(TestObj(3));
    EXPECT_EQ(TestObj::ConstructCount(), 3);
    EXPECT_EQ(TestObj::DestructCount(), 1);

    buffer.clear();
    EXPECT_EQ(TestObj::ConstructCount(), 3);
    EXPECT_EQ(TestObj::DestructCount(), 3);
    EXPECT_EQ(TestObj::ConstructCount(), TestObj::DestructCount());

    // Push two more objects and then let the RingBuffer go out of scope.
    buffer.push(TestObj(1));
    EXPECT_EQ(TestObj::ConstructCount(), 4);
    EXPECT_EQ(TestObj::DestructCount(), 3);

    buffer.emplace(2);
    EXPECT_EQ(TestObj::ConstructCount(), 5);
    EXPECT_EQ(TestObj::DestructCount(), 3);
  }

  // Assert that going out of scope called the destructors.
  EXPECT_EQ(TestObj::ConstructCount(), 5);
  EXPECT_EQ(TestObj::DestructCount(), 5);
}

}  // namespace
