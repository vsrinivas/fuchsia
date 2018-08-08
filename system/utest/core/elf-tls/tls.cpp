// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <threads.h>

#include <unittest/unittest.h>

static thread_local bool u1 = true;
static thread_local uint8_t u8 = UINT8_MAX;
static thread_local uint16_t u16 = UINT16_MAX;
static thread_local uint32_t u32 = UINT32_MAX;
static thread_local uint64_t u64 = UINT64_MAX;
static thread_local uintptr_t uptr = UINTPTR_MAX;
static thread_local int8_t i8 = INT8_MAX;
static thread_local int16_t i16 = INT16_MAX;
static thread_local int32_t i32 = INT32_MAX;
static thread_local int64_t i64 = INT64_MAX;
static thread_local intptr_t iptr = INTPTR_MAX;
static thread_local float f32 = FLT_MAX;
static thread_local double f64 = DBL_MAX;
static thread_local void* ptr = &ptr;
static thread_local struct {
    uint64_t bits0 : 9;
    uint64_t bits1 : 9;
    uint64_t bits2 : 9;
    uint64_t bits3 : 9;
    uint64_t bits4 : 9;
    uint64_t bits5 : 9;
    uint64_t bits6 : 9;
    double f64;
    uint64_t bits7 : 9;
    uint64_t bits8 : 9;
    uint64_t bits9 : 9;
    uint64_t bits10 : 9;
    uint64_t bits11 : 9;
    uint64_t bits12 : 9;
    uint64_t bits13 : 9;
} bits = {
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    DBL_MAX,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
    0x1ffu,
};
#define BYTES_4 0xffu, 0xffu, 0xffu, 0xffu,
#define BYTES_16 BYTES_4 BYTES_4 BYTES_4 BYTES_4
#define BYTES_64 BYTES_16 BYTES_16 BYTES_16 BYTES_16
#define BYTES_256 BYTES_64 BYTES_64 BYTES_64 BYTES_64
#define BYTES_1024 BYTES_256 BYTES_256 BYTES_256 BYTES_256
static thread_local uint8_t array[1024] = { BYTES_1024 };
static thread_local struct Ctor {
    Ctor() : x_(UINT64_MAX) {}
    uint64_t x_;
} ctor;
static thread_local uint8_t big_array[1 << 20];

__attribute__((aligned(0x1000))) thread_local int aligned_var = 123;

bool check_initializers() {
    BEGIN_TEST;

    ASSERT_EQ(u1, true, "unexpected initialized value");
    ASSERT_EQ(u8, UINT8_MAX, "unexpected initialized value");
    ASSERT_EQ(u16, UINT16_MAX, "unexpected initialized value");
    ASSERT_EQ(u32, UINT32_MAX, "unexpected initialized value");
    ASSERT_EQ(u64, UINT64_MAX, "unexpected initialized value");
    ASSERT_EQ(uptr, UINTPTR_MAX, "unexpected initialized value");
    ASSERT_EQ(i8, INT8_MAX, "unexpected initialized value");
    ASSERT_EQ(i16, INT16_MAX, "unexpected initialized value");
    ASSERT_EQ(i32, INT32_MAX, "unexpected initialized value");
    ASSERT_EQ(i64, INT64_MAX, "unexpected initialized value");
    ASSERT_EQ(iptr, INTPTR_MAX, "unexpected initialized value");
    ASSERT_EQ(f32, FLT_MAX, "unexpected initialized value");
    ASSERT_EQ(f64, DBL_MAX, "unexpected initialized value");
    ASSERT_EQ(ptr, &ptr, "unexpected initialized value");

    ASSERT_EQ(bits.bits0, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits1, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits2, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits3, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits4, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits5, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits6, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.f64, DBL_MAX, "unexpected initialized value");
    ASSERT_EQ(bits.bits7, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits8, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits9, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits10, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits11, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits12, 0x1ffu, "unexpected initialized value");
    ASSERT_EQ(bits.bits13, 0x1ffu, "unexpected initialized value");

    for (auto& byte : array)
        ASSERT_EQ(byte, UINT8_MAX, "unexpected initialized value");

    ASSERT_EQ(ctor.x_, UINT64_MAX, "unexpected initialized value");

    uint8_t sum = 0u;
    for (auto& byte : big_array)
        sum |= byte;
    ASSERT_EQ(sum, 0u, "unexpected initialized value");

    // TODO(ZX-1646): Make this work on ARM64.
    EXPECT_EQ((uintptr_t)&aligned_var % 0x1000, 0);
    EXPECT_EQ(aligned_var, 123);

    END_TEST;
}

bool test_array_spam(uintptr_t idx) {
    BEGIN_TEST;

    for (uintptr_t iteration = 0; iteration < 100; ++iteration) {
        auto starting_value = static_cast<uint8_t>(idx + iteration);
        auto value = starting_value;
        for (auto& byte : array) {
            byte = value;
            ++value;
        }
        sched_yield();
        value = starting_value;
        for (auto& byte : array) {
            ASSERT_EQ(byte, value, "unexpected value read back!");
            ++value;
        }
    }

    END_TEST;
}

int test_thread(void* arg) {
    auto idx = reinterpret_cast<uintptr_t>(arg);

    check_initializers();
    test_array_spam(idx);
    return 0;
}

bool executable_tls_test() {
    BEGIN_TEST;

    constexpr uintptr_t thread_count = 64u;
    thrd_t threads[thread_count];
    for (uintptr_t idx = 0u; idx < thread_count; ++idx) {
        auto arg = reinterpret_cast<void*>(idx);
        int ret = thrd_create_with_name(&threads[idx], &test_thread, arg, "elf tls test");
        ASSERT_EQ(ret, thrd_success, "unable to create test thread");
    }
    for (uintptr_t idx = 0u; idx < thread_count; ++idx) {
        int ret = thrd_join(threads[idx], nullptr);
        ASSERT_EQ(ret, thrd_success, "unable to join test thread");
    }

    test_thread(nullptr);

    END_TEST;
}

BEGIN_TEST_CASE(elf_tls_tests)
RUN_TEST(executable_tls_test)
END_TEST_CASE(elf_tls_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
