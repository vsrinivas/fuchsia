// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/lazy_init/lazy_init.h>
#include <zircon/compiler.h>

#include <atomic>
#include <cstring>

#include <zxtest/zxtest.h>

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
  void ConstMethod() const {}

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
void lazy_init_test() {
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

  const auto dereference_test = [] { test_value->Method(); };

  if (Check != CheckType::None) {
    ASSERT_DEATH(dereference_test, "Testing assert before initialization.\n");
  } else {
    ASSERT_NO_DEATH(dereference_test, "Testing assert before initialization.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  const auto initialization_test = [] { test_value.Initialize(); };

  ASSERT_NO_DEATH(initialization_test, "Testing initialization.\n");
  ++expected_constructions;

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  // Make sure that the const accessors (Get and the -> operator) are defined
  // for each specialization of LazyInit.
  const LazyInitType& const_test_value = test_value_storage.value;
  const_test_value.Get().ConstMethod();  // Get()
  const_test_value->ConstMethod();       // -> operator
  const_test_value.GetAddressUnchecked()->ConstMethod();

  if (Check == CheckType::None) {
    ASSERT_NO_DEATH(initialization_test, "Testing re-initialization.\n");
    ++expected_constructions;
  } else {
    ASSERT_DEATH(initialization_test, "Testing re-initialization.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  ASSERT_NO_DEATH(dereference_test, "Testing assert after initialization.\n");

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  const auto destruction_test = [] { test_value.LazyInitType::~LazyInitType(); };

  ASSERT_NO_DEATH(destruction_test, "Testing destruction.\n");

  if (Enabled == Destructor::Enabled) {
    ++expected_destructions;
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  if (Check == CheckType::None || Enabled == Destructor::Disabled) {
    ASSERT_NO_DEATH(dereference_test, "Testing assert after destruction.\n");
  } else {
    ASSERT_DEATH(dereference_test, "Testing assert after destruction.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());

  if (Check == CheckType::None || Enabled == Destructor::Disabled) {
    ASSERT_NO_DEATH(destruction_test, "Testing re-destruction.\n");
    if (Enabled == Destructor::Enabled) {
      ++expected_destructions;
    }
  } else {
    ASSERT_DEATH(destruction_test, "Testing re-destruction.\n");
  }

  EXPECT_EQ(expected_constructions, Type::constructions());
  EXPECT_EQ(expected_destructions, Type::destructions());
}

// TODO(eieio): Does it make sense to try to create races to test the atomic
// check specialization more thoroughly?

TEST(LazyInitTest, NoCheckNoDtor) { lazy_init_test<CheckType::None, Destructor::Disabled>(); }

TEST(LazyInitTest, BasicChecksNoDtor) { lazy_init_test<CheckType::Basic, Destructor::Disabled>(); }

TEST(LazyInitTest, AtomicChecksNoDtor) {
  lazy_init_test<CheckType::Atomic, Destructor::Disabled>();
}

TEST(LazyInitTest, NoChecksWithDtor) { lazy_init_test<CheckType::None, Destructor::Enabled>(); }

TEST(LazyInitTest, BasicChecksWithDtor) { lazy_init_test<CheckType::Basic, Destructor::Enabled>(); }

TEST(LazyInitTest, AtomicChecksWithDtor) {
  lazy_init_test<CheckType::Atomic, Destructor::Enabled>();
}

class TypeWithPrivateCtor {
  friend lazy_init::Access;
  explicit TypeWithPrivateCtor(int arg) {}
};

// Verify that LazyInit can be used with private constructors as long as they befriend LazyInit.
TEST(LazyInitTest, PrivateCtor) {
  LazyInit<TypeWithPrivateCtor, CheckType::None, Destructor::Disabled> instance;
  instance.Initialize(0);
}

// Verify the initialization guard is initialized during LazyInit's construction.
TEST(LazyInitTest, InitializeGuardIsInitialized) {
  {
    LazyInit<TestType<>, CheckType::Basic> basic_instance;
    basic_instance.Initialize();
  }
  {
    LazyInit<TestType<>, CheckType::Atomic> atomic_instance;
    atomic_instance.Initialize();
  }
}

}  // anonymous namespace
