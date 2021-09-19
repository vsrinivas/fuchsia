// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/stdcompat/internal/atomic.h"

#include <lib/stdcompat/atomic.h>

#include <array>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace {

using int128_t = __int128;
using uint128_t = unsigned __int128;

// For signature name, the specialization type T, can be used.
#define ATOMIC_REF_HAS_METHOD_TRAIT(trait_name, fn_name, sig)            \
  template <typename T>                                                  \
  struct trait_name {                                                    \
   private:                                                              \
    template <typename C>                                                \
    static std::true_type test(decltype(static_cast<sig>(&C::fn_name))); \
    template <typename C>                                                \
    static std::false_type test(...);                                    \
                                                                         \
   public:                                                               \
    static constexpr bool value = decltype(test<T>(nullptr))::value;     \
  };                                                                     \
  template <typename T>                                                  \
  static inline constexpr bool trait_name##_v = trait_name<T>::value

ATOMIC_REF_HAS_METHOD_TRAIT(specialization_has_fetch_sub, fetch_add,
                            typename T::value_type (C::*)(typename T::difference_type,
                                                          std::memory_order) const noexcept);
ATOMIC_REF_HAS_METHOD_TRAIT(specialization_has_fetch_add, fetch_sub,
                            typename T::value_type (C::*)(typename T::difference_type,
                                                          std::memory_order) const noexcept);

ATOMIC_REF_HAS_METHOD_TRAIT(specialization_has_fetch_or, fetch_or,
                            typename T::value_type (C::*)(typename T::difference_type,
                                                          std::memory_order) const noexcept);
ATOMIC_REF_HAS_METHOD_TRAIT(specialization_has_fetch_and, fetch_and,
                            typename T::value_type (C::*)(typename T::difference_type,
                                                          std::memory_order) const noexcept);
ATOMIC_REF_HAS_METHOD_TRAIT(specialization_has_fetch_xor, fetch_xor,
                            typename T::value_type (C::*)(typename T::difference_type,
                                                          std::memory_order) const noexcept);

template <typename T>
void CheckIntegerSpecialization() {
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::value_type, T>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::difference_type, T>, "");
  static_assert(cpp20::atomic_ref<T>::required_alignment == std::max(alignof(T), sizeof(T)), "");
}

template <typename T, typename U>
std::enable_if_t<cpp17::is_pointer_v<T>, T> cast_helper(U u) {
  return reinterpret_cast<T>(u);
}

template <typename T, typename U>
std::enable_if_t<!cpp17::is_pointer_v<T>, T> cast_helper(U u) {
  return static_cast<T>(u);
}

struct CheckAtomicOps {
  // For const types this operations would fail to compile.
  template <typename T>
  static void CheckStoreAndExchange() {}

  template <typename T, typename std::enable_if_t<!cpp17::is_const_v<T>>>
  static void CheckStoreAndExchange() {
    T zero = cast_helper<T>(0);
    T one = cast_helper<T>(1);

    T obj = zero;
    cpp20::atomic_ref<T> ref(obj);

    ref.store(one);
    EXPECT_EQ(obj, one);

    obj = zero;
    EXPECT_EQ(ref.exchange(one), zero);
    EXPECT_EQ(obj, one);

    // Assignment
    ref = zero;
    EXPECT_EQ(obj, zero);
  }

  // For volatiles this operations would fail to compile.
  template <typename T>
  static void CheckCompareExchange() {}

  template <typename T, typename std::enable_if_t<cpp20::atomic_internal::unqualified<T>>>
  static void CheckCheckCompareExchange() {
    T zero = cast_helper<T>(0);
    T one = cast_helper<T>(1);

    T obj = zero;
    cpp20::atomic_ref<T> ref(obj);

    obj = zero;
    T exp = one;
    EXPECT_FALSE(ref.compare_exchange_weak(exp, zero));
    EXPECT_EQ(obj, zero);

    obj = zero;
    exp = one;
    EXPECT_FALSE(ref.compare_exchange_strong(exp, zero));
    EXPECT_EQ(obj, zero);

    obj = zero;
    exp = zero;
    EXPECT_TRUE(ref.compare_exchange_weak(exp, one));
    EXPECT_EQ(obj, one);

    obj = zero;
    exp = zero;
    EXPECT_TRUE(ref.compare_exchange_strong(exp, one));
    EXPECT_EQ(obj, one);
  }
};

template <typename T>
void CheckAtomicOperations() {
  T zero = cast_helper<T>(0);
  T one = cast_helper<T>(1);

  T obj = zero;
  cpp20::atomic_ref<T> ref(obj);
  EXPECT_EQ(ref.load(), zero);

  T obj2 = one;
  cpp20::atomic_ref<T> ref2(obj2);
  EXPECT_EQ(ref2.load(), one);

  // Copy.
  cpp20::atomic_ref<T> ref_cpy(ref);
  EXPECT_EQ(ref_cpy.load(), ref.load());

  // Operator T.
  T val = ref;
  EXPECT_EQ(val, ref.load());

  // store, exchange and compare_and_exchange depends on |T|'s qualification.
  // * |const T| does not support any mutable operation.
  // * |volatile T| does not support compare_and_exchange operations.
  //
  // The following checks, will be No-Op depending on |T|.
  CheckAtomicOps::CheckStoreAndExchange<T>();
  CheckAtomicOps::CheckCompareExchange<T>();
}

template <typename T>
constexpr void CheckIntegerOperations() {
  T obj = 20;
  cpp20::atomic_ref<T> ref(obj);

  {
    T prev = ref.load();
    EXPECT_EQ(ref.fetch_add(4), prev);
    EXPECT_EQ(ref.load(), prev + 4);
    EXPECT_EQ(ref.load(), obj);

    T val = ref;
    EXPECT_EQ(val, ref.load());
  }

  {
    T prev = ref.load();
    EXPECT_EQ(ref.fetch_sub(4), prev);
    EXPECT_EQ(ref.load(), prev - 4);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T prev = ref.load();
    EXPECT_EQ((ref += 4), prev + 4);
    EXPECT_EQ(ref.load(), prev + 4);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T prev = ref.load();
    EXPECT_EQ((ref -= 4), prev - 4);
    EXPECT_EQ(ref.load(), prev - 4);
    EXPECT_EQ(ref.load(), obj);
  }

  T mask = static_cast<T>(0b01010101);

  {
    obj = -1;
    EXPECT_EQ(ref.fetch_and(mask), static_cast<T>(-1));
    EXPECT_EQ(obj, mask);

    obj = -1;
    EXPECT_EQ(ref &= mask, mask);
    EXPECT_EQ(obj, mask);

    obj = ~mask;
    EXPECT_EQ(ref.fetch_and(mask), static_cast<T>(~mask));
    EXPECT_EQ(obj, static_cast<T>(0));

    obj = ~mask;
    EXPECT_EQ(ref &= mask, static_cast<T>(0));
    EXPECT_EQ(obj, static_cast<T>(0));
  }

  {
    obj = 0;
    EXPECT_EQ(ref.fetch_or(mask), static_cast<T>(0));
    EXPECT_EQ(obj, mask);

    obj = 0;
    EXPECT_EQ(ref |= mask, mask);
    EXPECT_EQ(obj, mask);

    obj = -1;
    EXPECT_EQ(ref.fetch_or(mask), static_cast<T>(-1));
    EXPECT_EQ(obj, static_cast<T>(-1));

    obj = ~mask;
    EXPECT_EQ(ref |= mask, static_cast<T>(-1));
    EXPECT_EQ(obj, static_cast<T>(-1));
  }

  {
    obj = -1;
    EXPECT_EQ(ref.fetch_xor(mask), static_cast<T>(-1));
    EXPECT_EQ(obj, static_cast<T>(~mask));

    obj = -1;
    EXPECT_EQ(ref ^= mask, static_cast<T>(~mask));
    EXPECT_EQ(obj, static_cast<T>(~mask));

    obj = ~mask;
    EXPECT_EQ(ref.fetch_xor(mask), static_cast<T>(~mask));
    EXPECT_EQ(obj, static_cast<T>(-1));

    obj = ~mask;
    EXPECT_EQ(ref ^= mask, static_cast<T>(-1));
    EXPECT_EQ(obj, static_cast<T>(-1));
  }
}

template <typename T>
void CheckFloatSpecialization() {
  static_assert(!specialization_has_fetch_and_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_or_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_xor_v<cpp20::atomic_ref<T>>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::value_type, T>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::difference_type, T>, "");
  static_assert(cpp20::atomic_ref<T>::required_alignment == alignof(T), "");
}

template <typename T>
constexpr void CheckFloatOperations() {
  T obj = 4.f;
  cpp20::atomic_ref<T> ref(obj);

  {
    T prev = ref.load();
    EXPECT_EQ(ref.fetch_add(4.f), prev);
    EXPECT_EQ(ref.load(), prev + 4.f);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T prev = ref.load();
    EXPECT_EQ(ref.fetch_sub(4.f), prev);
    EXPECT_EQ(ref.load(), prev - 4.f);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T prev = ref.load();
    EXPECT_EQ((ref += 4.f), prev + 4.f);
    EXPECT_EQ(ref.load(), prev + 4.f);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T prev = ref.load();
    EXPECT_EQ((ref -= 4.f), prev - 4.f);
    EXPECT_EQ(ref.load(), prev - 4.f);
    EXPECT_EQ(ref.load(), obj);
  }
}

template <typename T>
constexpr void CheckPointerSpecialization() {
  static_assert(!specialization_has_fetch_and_v<cpp20::atomic_ref<T*>>, "");
  static_assert(!specialization_has_fetch_or_v<cpp20::atomic_ref<T*>>, "");
  static_assert(!specialization_has_fetch_xor_v<cpp20::atomic_ref<T*>>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T*>::value_type, T*>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T*>::difference_type, ptrdiff_t>, "");
  static_assert(cpp20::atomic_ref<T*>::required_alignment == alignof(T*), "");
}

template <typename T>
constexpr void CheckPointerOperations() {
  T a = {};
  T* obj = &a;
  cpp20::atomic_ref<T*> ref(obj);

  {
    T* prev = ref.load();
    EXPECT_EQ(ref.fetch_add(4), prev);
    EXPECT_EQ(ref.load(), prev + 4);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T* prev = ref.load();
    EXPECT_EQ(ref.fetch_sub(4), prev);
    EXPECT_EQ(ref.load(), prev - 4);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T* prev = ref.load();
    EXPECT_EQ((ref += 4), prev + 4);
    EXPECT_EQ(ref.load(), prev + 4);
    EXPECT_EQ(ref.load(), obj);
  }

  {
    T* prev = ref.load();
    EXPECT_EQ((ref -= 4), prev - 4);
    EXPECT_EQ(ref.load(), prev - 4);
    EXPECT_EQ(ref.load(), obj);
  }
}

template <typename T>
void CheckNoSpecialization() {
  static_assert(!specialization_has_fetch_add_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_sub_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_and_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_or_v<cpp20::atomic_ref<T>>, "");
  static_assert(!specialization_has_fetch_xor_v<cpp20::atomic_ref<T>>, "");

  static_assert(cpp20::atomic_ref<T>::required_alignment >= alignof(T), "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::value_type, T>, "");
  static_assert(cpp17::is_same_v<typename cpp20::atomic_ref<T>::difference_type, T>, "");
}

// Trivially copyable struct.
struct TriviallyCopyable {
  TriviallyCopyable(int a) : a(a) {}

  bool operator==(const TriviallyCopyable& rhs) const { return a == rhs.a; }

  bool operator!=(const TriviallyCopyable& rhs) const { return a != rhs.a; }

  int a = 0;
};

static_assert(cpp17::is_trivially_copyable_v<TriviallyCopyable>, "");

TEST(AtomicRefTest, NoSpecialization) {
  CheckAtomicOperations<bool>();
  CheckNoSpecialization<bool>();

  CheckAtomicOperations<TriviallyCopyable>();
  CheckNoSpecialization<TriviallyCopyable>();

  CheckAtomicOperations<volatile int>();
  CheckNoSpecialization<volatile int>();

  CheckAtomicOperations<volatile float>();
  CheckNoSpecialization<volatile float>();

  CheckAtomicOperations<const int>();
  CheckNoSpecialization<const int>();
}

TEST(AtomicRefTest, IntegerSpecialization) {
  CheckAtomicOperations<int>();
  CheckIntegerSpecialization<int>();
  CheckIntegerOperations<int>();

  // 1 byte
  CheckAtomicOperations<char>();
  CheckIntegerSpecialization<char>();
  CheckIntegerOperations<char>();

  CheckAtomicOperations<uint8_t>();
  CheckIntegerSpecialization<uint8_t>();
  CheckIntegerOperations<uint8_t>();

  // 8 bytes
  CheckAtomicOperations<int64_t>();
  CheckIntegerSpecialization<int64_t>();
  CheckIntegerOperations<int64_t>();

  CheckAtomicOperations<uint64_t>();
  CheckIntegerSpecialization<uint64_t>();
  CheckIntegerOperations<uint64_t>();

  // 16 bytes -- if supported, to silence oversized atomic operations.
  if (!cpp20::atomic_ref<int128_t>::is_always_lockfree) {
    return;
  }
  CheckAtomicOperations<int128_t*>();
  CheckIntegerSpecialization<int128_t>();
  CheckIntegerOperations<int128_t>();

  CheckAtomicOperations<uint128_t*>();
  CheckIntegerSpecialization<uint128_t>();
  CheckIntegerOperations<uint128_t>();
}

TEST(AtomicRefTest, FloatSpecialization) {
  CheckAtomicOperations<float>();
  CheckFloatSpecialization<float>();
  CheckFloatOperations<float>();

  CheckAtomicOperations<double>();
  CheckFloatSpecialization<double>();
  CheckFloatOperations<double>();
}

TEST(AtomicRefTest, PointerSpecialization) {
  CheckAtomicOperations<uint8_t*>();
  CheckPointerSpecialization<uint8_t>();
  CheckPointerOperations<uint8_t>();

  CheckAtomicOperations<int8_t*>();
  CheckPointerSpecialization<int8_t>();
  CheckPointerOperations<int8_t>();

  CheckAtomicOperations<uint64_t*>();
  CheckPointerSpecialization<uint64_t>();
  CheckPointerOperations<uint64_t>();
}

TEST(AtomicRefTest, ConstFromNonConst) {
  int a = 12345;

  // From non const T.
  cpp20::atomic_ref<const int> b(a);
  EXPECT_EQ(b.load(), a);
}

#if __cpp_lib_atomic_ref >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(AtomicRefTest, IsAliasForStd) {
  static_assert(
      std::is_same_v<cpp20::atomic_ref<TriviallyCopyable>, std::atomic_ref<TriviallyCopyable>>, "");
  static_assert(std::is_same_v<cpp20::atomic_ref<bool>, std::atomic_ref<bool>>, "");
  static_assert(std::is_same_v<cpp20::atomic_ref<int>, std::atomic_ref<int>>, "");
  static_assert(std::is_same_v<cpp20::atomic_ref<float>, std::atomic_ref<float>>, "");
  static_assert(std::is_same_v<cpp20::atomic_ref<int*>, std::atomic_ref<int*>>, "");
}

#endif

}  // namespace

namespace std {

std::ostream& operator<<(std::ostream& os, int128_t a) {
  int128_t mask = std::numeric_limits<int64_t>::min();
  os << "0x" << std::hex << static_cast<int64_t>(a >> 64) << static_cast<int64_t>(a & mask);
  return os;
}

std::ostream& operator<<(std::ostream& os, uint128_t a) {
  uint128_t mask = std::numeric_limits<uint64_t>::min();
  os << "0x" << std::hex << static_cast<uint64_t>(a >> 64) << static_cast<uint64_t>(a & mask);
  return os;
}

}  // namespace std
