// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/lazy_init/lazy_init.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>

#include <atomic>
#include <cstring>

using lazy_init::CheckType;
using lazy_init::Destructor;
using lazy_init::LazyInit;

namespace {

template <auto...>
struct TestType {
  TestType() { constructions_++; }
  ~TestType() { destructions_++; }

  TestType(const TestType&) = delete;
  TestType& operator=(const TestType&) = delete;

  void Method() {}

  static size_t constructions() { return constructions_.load(); }
  static size_t destructions() { return destructions_.load(); }

  inline static std::atomic<size_t> constructions_;
  inline static std::atomic<size_t> destructions_;
};

template <typename T>
union Storage {
  constexpr Storage() : value{} {}
  ~Storage() {}

  int dummy;
  T value;
};

template <CheckType Check, Destructor Enabled>
bool lazy_init_test() {
  BEGIN_TEST;

  // Define a unique test type for this test instantiation.
  using Type = TestType<Check, Enabled>;

  // Define a lazy-initialized variable for this test. Normally this
  // would be a static or global, but for this test we need to control
  // when the dtor is executed and avoid asserting at the end of the test
  // process when global dtors run.
  using LazyInitType = LazyInit<Type, Check, Enabled>;

  static Storage<LazyInitType> test_value_storage{};
  LazyInitType& test_value = test_value_storage.value;

  size_t expected_constructions = 0;
  size_t expected_destructions = 0;

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  const auto dereference_test = [](void* arg) {
    auto& lazy = *static_cast<LazyInitType*>(arg);
    lazy->Method();
  };

  if (Check != CheckType::None) {
    ASSERT_DEATH(dereference_test, &test_value, "Testing assert before initialization.\n");
  } else {
    ASSERT_NO_DEATH(dereference_test, &test_value, "Testing assert before initialization.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  const auto initialization_test = [](void* arg) {
    auto& lazy = *static_cast<LazyInitType*>(arg);
    lazy.Initialize();
  };

  ASSERT_NO_DEATH(initialization_test, &test_value, "Testing intialization.\n");
  ++expected_constructions;

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  if (Check == CheckType::None) {
    ASSERT_NO_DEATH(initialization_test, &test_value, "Testing re-intialization.\n");
    ++expected_constructions;
  } else {
    ASSERT_DEATH(initialization_test, &test_value, "Testing re-intialization.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  ASSERT_NO_DEATH(dereference_test, &test_value, "Testing assert after initialization.\n");

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  const auto destruction_test = [](void* arg) {
    auto& lazy = *static_cast<LazyInitType*>(arg);
    lazy.LazyInitType::~LazyInitType();
  };

  ASSERT_NO_DEATH(destruction_test, &test_value, "Testing destruction.\n");

  if (Enabled == Destructor::Enabled) {
    ++expected_destructions;
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  if (Check == CheckType::None || Enabled == Destructor::Disabled) {
    ASSERT_NO_DEATH(dereference_test, &test_value, "Testing assert after destruction.\n");
  } else {
    ASSERT_DEATH(dereference_test, &test_value, "Testing assert after destruction.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  if (Check == CheckType::None || Enabled == Destructor::Disabled) {
    ASSERT_NO_DEATH(destruction_test, &test_value, "Testing re-destruction.\n");
    if (Enabled == Destructor::Enabled) {
      ++expected_destructions;
    }
  } else {
    ASSERT_DEATH(destruction_test, &test_value, "Testing re-destruction.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  END_TEST;
}

// TODO(eieio): Does it make sense to try to create races to test the atomic
// check specialization more thoroughly?

}  // anonymous namespace

BEGIN_TEST_CASE(lazy_init_tests)
RUN_NAMED_TEST("Lazy init (no checks / no dtor)",
               (lazy_init_test<CheckType::None, Destructor::Disabled>))
RUN_NAMED_TEST("Lazy init (basic checks / no dtor)",
               (lazy_init_test<CheckType::Basic, Destructor::Disabled>))
RUN_NAMED_TEST("Lazy init (atomic checks / no dtor)",
               (lazy_init_test<CheckType::Atomic, Destructor::Disabled>))
RUN_NAMED_TEST("Lazy init (no checks / with dtor)",
               (lazy_init_test<CheckType::None, Destructor::Enabled>))
RUN_NAMED_TEST("Lazy init (basic checks / with dtor)",
               (lazy_init_test<CheckType::Basic, Destructor::Enabled>))
RUN_NAMED_TEST("Lazy init (atomic checks / with dtor)",
               (lazy_init_test<CheckType::Atomic, Destructor::Enabled>))
END_TEST_CASE(lazy_init_tests)
