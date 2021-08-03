// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/object_view.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/event.h>

#include <gtest/gtest.h>
#include <src/lib/fidl/llcpp/tests/types_test_utils.h>

// Tests the allocation of a uint32 vector which fits inside the initial buffer.
TEST(Arena, Uint32VectorConstructed) {
  fidl::Arena allocator;
  constexpr int kCount = 10;
  fidl::VectorView<uint32_t> vector(allocator, kCount);
  for (int i = 0; i < kCount; ++i) {
    vector[i] = i;
  }
}

// Tests that trivially destructible objects don't create deallocation data.
TEST(Arena, Uint32VectorDeallocation) {
  fidl::Arena allocator;
  constexpr int kCount = 10;
  fidl::VectorView<uint32_t> vector1(allocator, kCount);
  fidl::VectorView<uint32_t> vector2(allocator, kCount);
  // Checks that the second buffer has been allocated right after the first one.
  ASSERT_EQ(vector1.data() + kCount, vector2.data());
}

// Tests the allocation of big uint32 vectors. The first vector fits within the initial buffer.
// Each other vector needs an extra allocated block.
TEST(Arena, BigUint32VectorsConstructed) {
  fidl::Arena<4096> allocator;
  constexpr int kCount = 4000;
  fidl::VectorView<uint32_t> vector1(allocator, kCount);
  fidl::VectorView<uint32_t> vector2(allocator, kCount);
  fidl::VectorView<uint32_t> vector3(allocator, kCount);
  fidl::VectorView<uint32_t> vector4(allocator, kCount);
  for (int i = 0; i < kCount; ++i) {
    vector1[i] = i;
    vector2[i] = i;
    vector3[i] = i;
    vector4[i] = i;
  }
}

// Tests the allocation of a huge uint32 vector. The vector doesn't fit within the initial buffer
// and it doesn't fit within a standard extra block. That means that a tailored buffer is
// allocated to fit the vector.
TEST(Arena, HugeUint32VectorConstructed) {
  fidl::Arena<256> allocator;
  constexpr int kCount = 8000;
  fidl::VectorView<uint32_t> vector(allocator, kCount);
  for (int i = 0; i < kCount; ++i) {
    vector[i] = i;
  }
}

// Tests the allocation of an event vector which fits inside the initial buffer.
TEST(Arena, EventVectorConstructed) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena allocator;
    constexpr int kCount = 10;
    fidl::VectorView<zx::event> vector(allocator, kCount);
    for (int i = 0; i < kCount; ++i) {
      zx::event::create(0, &vector[i]);
      handle_checker.AddEvent(vector[i]);
    }
  }
  handle_checker.CheckEvents();
}

// Tests the allocation of an event vector which fits inside the initial buffer. The vector view's
// content is allocated after the construction of the vector view.
TEST(Arena, EventVectorAllocated) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena allocator;
    constexpr int kCount = 10;
    fidl::VectorView<zx::event> vector;
    vector.Allocate(allocator, kCount);
    for (int i = 0; i < kCount; ++i) {
      zx::event::create(0, &vector[i]);
      handle_checker.AddEvent(vector[i]);
    }
  }
  handle_checker.CheckEvents();
}

// Tests the allocation of an event vector which doesn't fit inside the initial buffer.
TEST(Arena, LargeEventVectorConstructed) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena<256> allocator;
    constexpr int kCount = 100;
    fidl::VectorView<zx::event> vector(allocator, kCount);
    for (int i = 0; i < kCount; ++i) {
      zx::event::create(0, &vector[i]);
      handle_checker.AddEvent(vector[i]);
    }
  }
  handle_checker.CheckEvents();
}

// Tests a mixed allocation. Each event vector is allocated in the remaining space within the block
// needed to allocate the previous uint32 vector.
TEST(Arena, MixedVectorConstructed) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena allocator;
    constexpr int kCountUint32 = 4000;
    constexpr int kCountEvent = 10;
    // Needs an extra block.
    fidl::VectorView<uint32_t> vector1(allocator, kCountUint32);
    // Fits within the current extra block.
    fidl::VectorView<zx::event> vector2(allocator, kCountEvent);
    // Needs another extra block.
    fidl::VectorView<uint32_t> vector3(allocator, kCountUint32);
    // Fits within the second extra block.
    fidl::VectorView<zx::event> vector4(allocator, kCountEvent);
    for (int i = 0; i < kCountEvent; ++i) {
      zx::event::create(0, &vector2[i]);
      handle_checker.AddEvent(vector2[i]);
      zx::event::create(0, &vector4[i]);
      handle_checker.AddEvent(vector4[i]);
    }
  }
  handle_checker.CheckEvents();
}

// Tests the allocation of strings.
TEST(Arena, StringConstructed) {
  fidl::Arena allocator;

  fidl::StringView empty_string(allocator, "");

  char buffer[100];
  strcpy(buffer, "hello");
  fidl::StringView hello(allocator, buffer);
  // Use the same buffer to check that the string is copied.
  strcpy(buffer, "world");
  fidl::StringView world(allocator, buffer);

  fidl::StringView hello2(allocator, hello.get());

  std::string buffer2("another string");
  fidl::StringView another_string(allocator, buffer2);
  // Use the same buffer to check that the string is copied.
  buffer2 = std::string("one last string");
  fidl::StringView one_last_string(allocator, buffer2);

  // Checks that all the allocations have been correctly done and that none of them clubber another
  // one.
  EXPECT_EQ(hello.get(), "hello");
  EXPECT_EQ(world.get(), "world");
  EXPECT_EQ(hello2.get(), "hello");
  EXPECT_EQ(another_string.get(), "another string");
  EXPECT_EQ(one_last_string.get(), "one last string");
}

// Tests the allocation of strings.
TEST(Arena, StringSet) {
  fidl::Arena allocator;

  fidl::StringView empty_string;
  empty_string.Set(allocator, "");

  char buffer[100];
  strcpy(buffer, "hello");
  fidl::StringView hello;
  hello.Set(allocator, buffer);
  // Use the same buffer to check that the string is copied.
  strcpy(buffer, "world");
  fidl::StringView world;
  world.Set(allocator, buffer);

  fidl::StringView hello2;
  hello2.Set(allocator, hello.get());

  std::string buffer2("another string");
  fidl::StringView another_string;
  another_string.Set(allocator, buffer2);
  // Use the same buffer to check that the string is copied.
  buffer2 = std::string("one last string");
  fidl::StringView one_last_string;
  one_last_string.Set(allocator, buffer2);

  // Checks that all the allocations have been correctly done and that none of them clubber another
  // one.
  EXPECT_EQ(hello.get(), "hello");
  EXPECT_EQ(world.get(), "world");
  EXPECT_EQ(hello2.get(), "hello");
  EXPECT_EQ(another_string.get(), "another string");
  EXPECT_EQ(one_last_string.get(), "one last string");
}

// Tests the allocation of a uint32 instance.
TEST(Arena, Uint32InstanceConstructedThenInitialized) {
  fidl::Arena allocator;
  fidl::ObjectView<uint32_t> instance_1(allocator);
  *instance_1 = 10;
  fidl::ObjectView<uint32_t> instance_2(allocator);
  *instance_2 = 20;
  EXPECT_EQ(*instance_1, 10U);
  EXPECT_EQ(*instance_2, 20U);
}

// Tests the allocation of a uint32 instance.
TEST(Arena, Uint32InstanceDirectlyConstructed) {
  fidl::Arena allocator;
  fidl::ObjectView<uint32_t> instance_1(allocator, 10);
  fidl::ObjectView<uint32_t> instance_2(allocator, 20);
  EXPECT_EQ(*instance_1, 10U);
  EXPECT_EQ(*instance_2, 20U);
}

// Tests the allocation of an event instance.
TEST(Arena, EventInstanceConstructed) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena allocator;
    fidl::ObjectView<zx::event> instance_1(allocator);
    zx::event::create(0, instance_1.get());
    handle_checker.AddEvent(*instance_1);

    fidl::ObjectView<zx::event> instance_2(allocator);
    zx::event::create(0, instance_2.get());
    handle_checker.AddEvent(*instance_2);
  }
  handle_checker.CheckEvents();
}

// Tests the allocation of a uint32 instance.
TEST(Arena, Uint32InstanceAllocatedThenInitialized) {
  fidl::Arena allocator;

  fidl::ObjectView<uint32_t> instance_1;
  fidl::ObjectView<uint32_t> instance_2;

  instance_1.Allocate(allocator);
  *instance_1 = 10;
  instance_2.Allocate(allocator);
  *instance_2 = 20;

  EXPECT_EQ(*instance_1, 10U);
  EXPECT_EQ(*instance_2, 20U);
}

// Tests the allocation of a uint32 instance.
TEST(Arena, Uint32InstanceDirectlyAllocated) {
  fidl::Arena allocator;

  fidl::ObjectView<uint32_t> instance_1;
  fidl::ObjectView<uint32_t> instance_2;

  instance_1.Allocate(allocator, 10);
  instance_2.Allocate(allocator, 20);

  EXPECT_EQ(*instance_1, 10U);
  EXPECT_EQ(*instance_2, 20U);
}

// Tests the allocation of an event instance.
TEST(Arena, EventInstanceAllocated) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  {
    fidl::Arena allocator;

    fidl::ObjectView<zx::event> instance_1;
    fidl::ObjectView<zx::event> instance_2;

    instance_1.Allocate(allocator);
    zx::event::create(0, instance_1.get());
    handle_checker.AddEvent(*instance_1);

    instance_2.Allocate(allocator);
    zx::event::create(0, instance_2.get());
    handle_checker.AddEvent(*instance_2);
  }
  handle_checker.CheckEvents();
}
