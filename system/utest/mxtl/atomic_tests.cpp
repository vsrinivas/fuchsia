// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/atomic.h>

#include <mxtl/limits.h>
#include <unittest/unittest.h>

namespace {

bool atomic_explicit_declarations_test() {
    BEGIN_TEST;

    mxtl::atomic<char> zero_char(0);
    mxtl::atomic<signed char> zero_schar(0);
    mxtl::atomic<unsigned char> zero_uchar(0);
    mxtl::atomic<short> zero_short(0);
    mxtl::atomic<unsigned short> zero_ushort(0);
    mxtl::atomic<int> zero_int(0);
    mxtl::atomic<unsigned int> zero_uint(0);
    mxtl::atomic<long> zero_long(0);
    mxtl::atomic<unsigned long> zero_ulong(0);
    mxtl::atomic<long long> zero_llong(0);
    mxtl::atomic<unsigned long long> zero_ullong(0);

    mxtl::atomic<intptr_t> zero_intptr_t(0);
    mxtl::atomic<uintptr_t> zero_uintptr_t(0);
    mxtl::atomic<size_t> zero_size_t(0);
    mxtl::atomic<ptrdiff_t> zero_ptrdiff_t(0);
    mxtl::atomic<intmax_t> zero_intmax_t(0);
    mxtl::atomic<uintmax_t> zero_uintmax_t(0);

    mxtl::atomic<int8_t> zero_int8_t(0);
    mxtl::atomic<uint8_t> zero_uint8_t(0);
    mxtl::atomic<int16_t> zero_int16_t(0);
    mxtl::atomic<uint16_t> zero_uint16_t(0);
    mxtl::atomic<int32_t> zero_int32_t(0);
    mxtl::atomic<uint32_t> zero_uint32_t(0);
    mxtl::atomic<int64_t> zero_int64_t(0);
    mxtl::atomic<uint64_t> zero_uint64_t(0);

    mxtl::atomic<int_least8_t> zero_int_least8_t(0);
    mxtl::atomic<uint_least8_t> zero_uint_least8_t(0);
    mxtl::atomic<int_least16_t> zero_int_least16_t(0);
    mxtl::atomic<uint_least16_t> zero_uint_least16_t(0);
    mxtl::atomic<int_least32_t> zero_int_least32_t(0);
    mxtl::atomic<uint_least32_t> zero_uint_least32_t(0);
    mxtl::atomic<int_least64_t> zero_int_least64_t(0);
    mxtl::atomic<uint_least64_t> zero_uint_least64_t(0);
    mxtl::atomic<int_fast8_t> zero_int_fast8_t(0);
    mxtl::atomic<uint_fast8_t> zero_uint_fast8_t(0);
    mxtl::atomic<int_fast16_t> zero_int_fast16_t(0);
    mxtl::atomic<uint_fast16_t> zero_uint_fast16_t(0);
    mxtl::atomic<int_fast32_t> zero_int_fast32_t(0);
    mxtl::atomic<uint_fast32_t> zero_uint_fast32_t(0);
    mxtl::atomic<int_fast64_t> zero_int_fast64_t(0);
    mxtl::atomic<uint_fast64_t> zero_uint_fast64_t(0);

    END_TEST;
}

bool atomic_using_declarations_test() {
    BEGIN_TEST;

    mxtl::atomic_char zero_char(0);
    mxtl::atomic_schar zero_schar(0);
    mxtl::atomic_uchar zero_uchar(0);
    mxtl::atomic_short zero_short(0);
    mxtl::atomic_ushort zero_ushort(0);
    mxtl::atomic_int zero_int(0);
    mxtl::atomic_uint zero_uint(0);
    mxtl::atomic_long zero_long(0);
    mxtl::atomic_ulong zero_ulong(0);
    mxtl::atomic_llong zero_llong(0);
    mxtl::atomic_ullong zero_ullong(0);

    mxtl::atomic_intptr_t zero_intptr_t(0);
    mxtl::atomic_uintptr_t zero_uintptr_t(0);
    mxtl::atomic_size_t zero_size_t(0);
    mxtl::atomic_ptrdiff_t zero_ptrdiff_t(0);
    mxtl::atomic_intmax_t zero_intmax_t(0);
    mxtl::atomic_uintmax_t zero_uintmax_t(0);

    mxtl::atomic_int8_t zero_int8_t(0);
    mxtl::atomic_uint8_t zero_uint8_t(0);
    mxtl::atomic_int16_t zero_int16_t(0);
    mxtl::atomic_uint16_t zero_uint16_t(0);
    mxtl::atomic_int32_t zero_int32_t(0);
    mxtl::atomic_uint32_t zero_uint32_t(0);
    mxtl::atomic_int64_t zero_int64_t(0);
    mxtl::atomic_uint64_t zero_uint64_t(0);

    mxtl::atomic_int_least8_t zero_int_least8_t(0);
    mxtl::atomic_uint_least8_t zero_uint_least8_t(0);
    mxtl::atomic_int_least16_t zero_int_least16_t(0);
    mxtl::atomic_uint_least16_t zero_uint_least16_t(0);
    mxtl::atomic_int_least32_t zero_int_least32_t(0);
    mxtl::atomic_uint_least32_t zero_uint_least32_t(0);
    mxtl::atomic_int_least64_t zero_int_least64_t(0);
    mxtl::atomic_uint_least64_t zero_uint_least64_t(0);
    mxtl::atomic_int_fast8_t zero_int_fast8_t(0);
    mxtl::atomic_uint_fast8_t zero_uint_fast8_t(0);
    mxtl::atomic_int_fast16_t zero_int_fast16_t(0);
    mxtl::atomic_uint_fast16_t zero_uint_fast16_t(0);
    mxtl::atomic_int_fast32_t zero_int_fast32_t(0);
    mxtl::atomic_uint_fast32_t zero_uint_fast32_t(0);
    mxtl::atomic_int_fast64_t zero_int_fast64_t(0);
    mxtl::atomic_uint_fast64_t zero_uint_fast64_t(0);

    END_TEST;
}

// To increase test readability after this point, static_assert that
// most of these are the same as mxtl::atomic<some other type>. That
// way no one has to read a million lines of test code about
// mxtl::atomic_uint_least32_t.

template <typename T>
constexpr bool IsSameAsSomeBuiltin() {
    return mxtl::is_same<T, mxtl::atomic_char>::value ||
           mxtl::is_same<T, mxtl::atomic_schar>::value ||
           mxtl::is_same<T, mxtl::atomic_uchar>::value ||
           mxtl::is_same<T, mxtl::atomic_short>::value ||
           mxtl::is_same<T, mxtl::atomic_ushort>::value ||
           mxtl::is_same<T, mxtl::atomic_int>::value ||
           mxtl::is_same<T, mxtl::atomic_uint>::value ||
           mxtl::is_same<T, mxtl::atomic_long>::value ||
           mxtl::is_same<T, mxtl::atomic_ulong>::value ||
           mxtl::is_same<T, mxtl::atomic_llong>::value ||
           mxtl::is_same<T, mxtl::atomic_ullong>::value;
}

static_assert(IsSameAsSomeBuiltin<mxtl::atomic_intptr_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uintptr_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_size_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_ptrdiff_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_intmax_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uintmax_t>(), "");

static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int64_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint64_t>(), "");

static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_least8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_least8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_least16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_least16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_least32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_least32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_least64_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_least64_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_fast8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_fast8_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_fast16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_fast16_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_fast32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_fast32_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_int_fast64_t>(), "");
static_assert(IsSameAsSomeBuiltin<mxtl::atomic_uint_fast64_t>(), "");

bool atomic_wont_compile_test() {
    BEGIN_TEST;

    // mxtl::atomic only supports integer types.

#if TEST_WILL_NOT_COMPILE || 0
    struct not_integral {};
    mxtl::atomic<not_integral> not_integral;
#endif

#if TEST_WILL_NOT_COMPILE || 0
    mxtl::atomic<float> not_integral;
#endif

#if TEST_WILL_NOT_COMPILE || 0
    mxtl::atomic<double> not_integral;
#endif

#if TEST_WILL_NOT_COMPILE || 0
    mxtl::atomic<void*> not_integral;
#endif

    END_TEST;
}

// Bunch of machinery for arithmetic tests.
template <typename T>
using ordinary_op = T (*)(T*, T);

template <typename T>
using atomic_op = T (*)(mxtl::atomic<T>*, T, mxtl::memory_order);

template <typename T>
using volatile_op = T (*)(volatile mxtl::atomic<T>*, T, mxtl::memory_order);

template <typename T>
struct TestCase {
    ordinary_op<T> ordinary;
    atomic_op<T> nonmember_atomic;
    atomic_op<T> member_atomic;
    volatile_op<T> nonmember_volatile;
    volatile_op<T> member_volatile;
};

template <typename T>
T test_values[] = {
    0,
    1,
    23,
    mxtl::numeric_limits<T>::min() / 4,
    mxtl::numeric_limits<T>::max() / 4,
};

template <typename T>
TestCase<T> test_cases[] = {
    {
        [](T* ptr_to_a, T b) -> T {
            T a = *ptr_to_a;
            *ptr_to_a = static_cast<T>(a + b);
            return a;
        },
        mxtl::atomic_fetch_add<T>,
        [](mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_add(b, order);
        },
        mxtl::atomic_fetch_add<T>,
        [](volatile mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_add(b, order);
        },
    },
    {
        [](T* ptr_to_a, T b) -> T {
            T a = *ptr_to_a;
            *ptr_to_a = static_cast<T>(a & b);
            return a;
        },
        mxtl::atomic_fetch_and<T>,
        [](mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_and(b, order);
        },
        mxtl::atomic_fetch_and<T>,
        [](volatile mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_and(b, order);
        },
    },
    {
        [](T* ptr_to_a, T b) -> T {
            T a = *ptr_to_a;
            *ptr_to_a = static_cast<T>(a | b);
            return a;
        },
        mxtl::atomic_fetch_or<T>,
        [](mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_or(b, order);
        },
        mxtl::atomic_fetch_or<T>,
        [](volatile mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_or(b, order);
        },
    },
    {
        [](T* ptr_to_a, T b) -> T {
            T a = *ptr_to_a;
            *ptr_to_a = static_cast<T>(a ^ b);
            return a;
        },
        mxtl::atomic_fetch_xor<T>,
        [](mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_xor(b, order);
        },
        mxtl::atomic_fetch_xor<T>,
        [](volatile mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
            return ptr_to_atomic_a->fetch_xor(b, order);
        },
    },
};

template <typename T>
TestCase<T> subtraction_test_case = {
    [](T* ptr_to_a, T b) -> T {
        T a = *ptr_to_a;
        *ptr_to_a = static_cast<T>(a - b);
        return a;
    },
    mxtl::atomic_fetch_sub,
    [](mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
        return ptr_to_atomic_a->fetch_sub(b, order);
    },
    mxtl::atomic_fetch_sub,
    [](volatile mxtl::atomic<T>* ptr_to_atomic_a, T b, mxtl::memory_order order) -> T {
        return ptr_to_atomic_a->fetch_sub(b, order);
    },
};

mxtl::memory_order orders[] = {
    mxtl::memory_order_relaxed,
    mxtl::memory_order_consume,
    mxtl::memory_order_acquire,
    mxtl::memory_order_release,
    mxtl::memory_order_acq_rel,
    mxtl::memory_order_seq_cst,
};

template <typename T>
bool math_test() {
    for (const T original_left : test_values<T>) {
        for (T right : test_values<T>) {
            for (const auto& order : orders) {
                for (auto test_case : test_cases<T>) {
                    {
                        mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(test_case.ordinary(&left, right),
                                  test_case.member_atomic(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(test_case.ordinary(&left, right),
                                  test_case.nonmember_atomic(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        volatile mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(test_case.ordinary(&left, right),
                                  test_case.member_volatile(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        volatile mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(test_case.ordinary(&left, right),
                                  test_case.nonmember_volatile(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                }
                // Let's not worry about signed subtraction UB.
                if (mxtl::is_unsigned<T>::value) {
                    {
                        mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(subtraction_test_case<T>.ordinary(&left, right),
                                  subtraction_test_case<T>.member_atomic(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(subtraction_test_case<T>.ordinary(&left, right),
                                  subtraction_test_case<T>.nonmember_atomic(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        volatile mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(subtraction_test_case<T>.ordinary(&left, right),
                                  subtraction_test_case<T>.member_volatile(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                    {
                        volatile mxtl::atomic<T> atomic_left(original_left);
                        T left = original_left;
                        ASSERT_EQ(subtraction_test_case<T>.ordinary(&left, right),
                                  subtraction_test_case<T>.nonmember_volatile(&atomic_left, right, order),
                                  "Atomic and ordinary math differ");
                        ASSERT_EQ(left, atomic_load(&atomic_left), "Atomic and ordinary math differ");
                    }
                }
            }
        }
    }

    return true;
}

template <typename T>
bool load_store_test() {
    mxtl::atomic<T> atomic_value;

    for (T value : test_values<T>) {
        atomic_value.store(value);
        ASSERT_EQ(atomic_value.load(), value, "Member load/store busted");
    }

    for (T value : test_values<T>) {
        mxtl::atomic_store(&atomic_value, value);
        ASSERT_EQ(atomic_load(&atomic_value), value, "Nonmember load/store busted");
    }

    volatile mxtl::atomic<T> volatile_value;

    for (T value : test_values<T>) {
        volatile_value.store(value);
        ASSERT_EQ(volatile_value.load(), value, "Member load/store busted");
    }

    for (T value : test_values<T>) {
        mxtl::atomic_store(&volatile_value, value);
        ASSERT_EQ(atomic_load(&volatile_value), value, "Nonmember load/store busted");
    }

    return true;
}

template <typename T>
bool exchange_test() {
    T last_value = test_values<T>[0];
    mxtl::atomic<T> atomic_value(last_value);

    for (T value : test_values<T>) {
        ASSERT_EQ(atomic_value.load(), last_value, "Member exchange busted");
        ASSERT_EQ(atomic_value.exchange(value), last_value, "Member exchange busted");
        last_value = value;
    }

    last_value = test_values<T>[0];
    atomic_value.store(last_value);

    for (T value : test_values<T>) {
        ASSERT_EQ(mxtl::atomic_load(&atomic_value), last_value, "Nonmember exchange busted");
        ASSERT_EQ(mxtl::atomic_exchange(&atomic_value, value), last_value, "Nonmember exchange busted");
        last_value = value;
    }

    last_value = test_values<T>[0];
    volatile mxtl::atomic<T> volatile_value(last_value);

    for (T value : test_values<T>) {
        ASSERT_EQ(volatile_value.load(), last_value, "Member exchange busted");
        ASSERT_EQ(volatile_value.exchange(value), last_value, "Member exchange busted");
        last_value = value;
    }

    last_value = test_values<T>[0];
    volatile_value.store(last_value);

    for (T value : test_values<T>) {
        ASSERT_EQ(mxtl::atomic_load(&volatile_value), last_value, "Nonmember exchange busted");
        ASSERT_EQ(mxtl::atomic_exchange(&volatile_value, value), last_value, "Nonmember exchange busted");
        last_value = value;
    }

    return true;
}

template <typename T>
struct cas_function {
    bool (*function)(mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
                     mxtl::memory_order success_order, mxtl::memory_order failure_order);
    bool can_spuriously_fail;
};

template <typename T>
cas_function<T> cas_functions[] = {
    {mxtl::atomic_compare_exchange_weak, true},
    {mxtl::atomic_compare_exchange_strong, false},
    {[](mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
        mxtl::memory_order success_order, mxtl::memory_order failure_order) {
         return atomic_ptr->compare_exchange_weak(expected, desired, success_order, failure_order);
     },
     true},
    {[](mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
        mxtl::memory_order success_order, mxtl::memory_order failure_order) {
         return atomic_ptr->compare_exchange_strong(expected, desired, success_order, failure_order);
     },
     false},
};

template <typename T>
struct volatile_cas_function {
    bool (*function)(volatile mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
                     mxtl::memory_order success_order, mxtl::memory_order failure_order);
    bool can_spuriously_fail;
};

template <typename T>
volatile_cas_function<T> volatile_cas_functions[] = {
    {mxtl::atomic_compare_exchange_weak, true},
    {mxtl::atomic_compare_exchange_strong, false},
    {[](volatile mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
        mxtl::memory_order success_order, mxtl::memory_order failure_order) {
         return atomic_ptr->compare_exchange_weak(expected, desired, success_order, failure_order);
     },
     true},
    {[](volatile mxtl::atomic<T>* atomic_ptr, T* expected, T desired,
        mxtl::memory_order success_order, mxtl::memory_order failure_order) {
         return atomic_ptr->compare_exchange_strong(expected, desired, success_order, failure_order);
     },
     false}};

template <typename T>
bool compare_exchange_test() {
    for (auto cas : cas_functions<T>) {
        for (const auto& success_order : orders) {
            for (const auto& failure_order : orders) {
                {
                    // Failure case
                    T actual = 23;
                    mxtl::atomic<T> atomic_value(actual);
                    T expected = 22;
                    T desired = 24;
                    EXPECT_FALSE(cas.function(&atomic_value, &expected, desired,
                                              success_order, failure_order),
                                 "compare-exchange shouldn't have succeeded!");
                    EXPECT_EQ(expected, actual, "compare-exchange didn't report actual value!");
                }
                {
                    // Success case
                    T actual = 23;
                    mxtl::atomic<T> atomic_value(actual);
                    T expected = actual;
                    T desired = 24;
                    // Some compare-and-swap functions can spuriously fail.
                    bool succeeded = cas.function(&atomic_value, &expected, desired,
                                                  success_order, failure_order);
                    if (!cas.can_spuriously_fail) {
                        EXPECT_TRUE(succeeded, "compare-exchange should've succeeded!");
                    }
                    EXPECT_EQ(expected, actual, "compare-exchange didn't report actual value!");
                }
            }
        }
    }

    for (auto cas : volatile_cas_functions<T>) {
        for (const auto& success_order : orders) {
            for (const auto& failure_order : orders) {
                {
                    // Failure case
                    T actual = 23;
                    mxtl::atomic<T> atomic_value(actual);
                    T expected = 22;
                    T desired = 24;
                    EXPECT_FALSE(cas.function(&atomic_value, &expected, desired,
                                              success_order, failure_order),
                                 "compare-exchange shouldn't have succeeded!");
                    EXPECT_EQ(expected, actual, "compare-exchange didn't report actual value!");
                }
                {
                    // Success case
                    T actual = 23;
                    mxtl::atomic<T> atomic_value(actual);
                    T expected = actual;
                    T desired = 24;
                    // Compare-and-swap can spuriously fail.
                    // Some compare-and-swap functions can spuriously fail.
                    bool succeeded = cas.function(&atomic_value, &expected, desired,
                                                  success_order, failure_order);
                    if (!cas.can_spuriously_fail) {
                        EXPECT_TRUE(succeeded, "compare-exchange should've succeeded!");
                    }
                    EXPECT_EQ(expected, actual, "compare-exchange didn't report actual value!");
                }
            }
        }
    }

    return true;
}

// Actual test cases on operations for each builtin type.
bool atomic_math_test() {
    BEGIN_TEST;

    ASSERT_TRUE(math_test<char>());
    ASSERT_TRUE(math_test<signed char>());
    ASSERT_TRUE(math_test<unsigned char>());
    ASSERT_TRUE(math_test<short>());
    ASSERT_TRUE(math_test<unsigned short>());
    ASSERT_TRUE(math_test<int>());
    ASSERT_TRUE(math_test<unsigned int>());
    ASSERT_TRUE(math_test<long>());
    ASSERT_TRUE(math_test<unsigned long>());
    ASSERT_TRUE(math_test<long long>());
    ASSERT_TRUE(math_test<unsigned long long>());

    END_TEST;
}

bool atomic_load_store_test() {
    BEGIN_TEST;

    ASSERT_TRUE(load_store_test<char>());
    ASSERT_TRUE(load_store_test<signed char>());
    ASSERT_TRUE(load_store_test<unsigned char>());
    ASSERT_TRUE(load_store_test<short>());
    ASSERT_TRUE(load_store_test<unsigned short>());
    ASSERT_TRUE(load_store_test<int>());
    ASSERT_TRUE(load_store_test<unsigned int>());
    ASSERT_TRUE(load_store_test<long>());
    ASSERT_TRUE(load_store_test<unsigned long>());
    ASSERT_TRUE(load_store_test<long long>());
    ASSERT_TRUE(load_store_test<unsigned long long>());

    END_TEST;
}

bool atomic_exchange_test() {
    BEGIN_TEST;

    ASSERT_TRUE(exchange_test<char>());
    ASSERT_TRUE(exchange_test<signed char>());
    ASSERT_TRUE(exchange_test<unsigned char>());
    ASSERT_TRUE(exchange_test<short>());
    ASSERT_TRUE(exchange_test<unsigned short>());
    ASSERT_TRUE(exchange_test<int>());
    ASSERT_TRUE(exchange_test<unsigned int>());
    ASSERT_TRUE(exchange_test<long>());
    ASSERT_TRUE(exchange_test<unsigned long>());
    ASSERT_TRUE(exchange_test<long long>());
    ASSERT_TRUE(exchange_test<unsigned long long>());

    END_TEST;
}

bool atomic_compare_exchange_test() {
    BEGIN_TEST;

    ASSERT_TRUE(compare_exchange_test<char>());
    ASSERT_TRUE(compare_exchange_test<signed char>());
    ASSERT_TRUE(compare_exchange_test<unsigned char>());
    ASSERT_TRUE(compare_exchange_test<short>());
    ASSERT_TRUE(compare_exchange_test<unsigned short>());
    ASSERT_TRUE(compare_exchange_test<int>());
    ASSERT_TRUE(compare_exchange_test<unsigned int>());
    ASSERT_TRUE(compare_exchange_test<long>());
    ASSERT_TRUE(compare_exchange_test<unsigned long>());
    ASSERT_TRUE(compare_exchange_test<long long>());
    ASSERT_TRUE(compare_exchange_test<unsigned long long>());

    END_TEST;
}

// Code wants to rely on the ABI of mxtl::atomic types. This means
// matching the underlying types' size and alignment, and the class
// being standard layout.

static_assert(sizeof(mxtl::atomic<char>) == sizeof(char), "");
static_assert(sizeof(mxtl::atomic<signed char>) == sizeof(signed char), "");
static_assert(sizeof(mxtl::atomic<unsigned char>) == sizeof(unsigned char), "");
static_assert(sizeof(mxtl::atomic<short>) == sizeof(short), "");
static_assert(sizeof(mxtl::atomic<unsigned short>) == sizeof(unsigned short), "");
static_assert(sizeof(mxtl::atomic<int>) == sizeof(int), "");
static_assert(sizeof(mxtl::atomic<unsigned int>) == sizeof(unsigned int), "");
static_assert(sizeof(mxtl::atomic<long>) == sizeof(long), "");
static_assert(sizeof(mxtl::atomic<unsigned long>) == sizeof(unsigned long), "");
static_assert(sizeof(mxtl::atomic<long long>) == sizeof(long long), "");
static_assert(sizeof(mxtl::atomic<unsigned long long>) == sizeof(unsigned long long), "");

static_assert(alignof(mxtl::atomic<char>) == alignof(char), "");
static_assert(alignof(mxtl::atomic<signed char>) == alignof(signed char), "");
static_assert(alignof(mxtl::atomic<unsigned char>) == alignof(unsigned char), "");
static_assert(alignof(mxtl::atomic<short>) == alignof(short), "");
static_assert(alignof(mxtl::atomic<unsigned short>) == alignof(unsigned short), "");
static_assert(alignof(mxtl::atomic<int>) == alignof(int), "");
static_assert(alignof(mxtl::atomic<unsigned int>) == alignof(unsigned int), "");
static_assert(alignof(mxtl::atomic<long>) == alignof(long), "");
static_assert(alignof(mxtl::atomic<unsigned long>) == alignof(unsigned long), "");
static_assert(alignof(mxtl::atomic<long long>) == alignof(long long), "");
static_assert(alignof(mxtl::atomic<unsigned long long>) == alignof(unsigned long long), "");

static_assert(mxtl::is_standard_layout<mxtl::atomic<char>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<signed char>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<unsigned char>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<short>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<unsigned short>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<int>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<unsigned int>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<long>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<unsigned long>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<long long>>::value, "");
static_assert(mxtl::is_standard_layout<mxtl::atomic<unsigned long long>>::value, "");

bool atomic_fence_test() {
    BEGIN_TEST;

    for (const auto& order : orders) {
        atomic_thread_fence(order);
        atomic_signal_fence(order);
    }

    END_TEST;
}

bool atomic_init_test() {
    BEGIN_TEST;

    mxtl::atomic_uint32_t atomic1;
    mxtl::atomic_init(&atomic1, 1u);
    EXPECT_EQ(1u, atomic1.load());

    mxtl::atomic_uint32_t atomic2;
    volatile mxtl::atomic_uint32_t* vatomic2 = &atomic2;
    mxtl::atomic_init(vatomic2, 2u);
    EXPECT_EQ(2u, atomic2.load());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(atomic_tests)
RUN_NAMED_TEST("Atomic explicit declarations test", atomic_explicit_declarations_test)
RUN_NAMED_TEST("Atomic using declarations test", atomic_using_declarations_test)
RUN_NAMED_TEST("Atomic won't compile test", atomic_wont_compile_test)
RUN_NAMED_TEST("Atomic math test", atomic_math_test)
RUN_NAMED_TEST("Atomic load/store test", atomic_load_store_test)
RUN_NAMED_TEST("Atomic exchange test", atomic_exchange_test)
RUN_NAMED_TEST("Atomic compare-exchange test", atomic_compare_exchange_test)
RUN_NAMED_TEST("Atomic fence test", atomic_fence_test)
RUN_NAMED_TEST("Atomic init test", atomic_init_test)
END_TEST_CASE(atomic_tests);
