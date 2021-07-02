// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/bit.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/testonly-syscalls.h>

#include <zxtest/zxtest.h>

namespace {

// This does a reinterpret_cast for a function pointer to a system call,
// ensuring that the compiler can't see through and make any assumptions about
// the original system call function's type signature.
template <typename T, typename U>
T* SyscallAs(U* syscall) {
  T* result;
  __asm__("" : "=g"(result) : "0"(syscall));
  return result;
}

TEST(SyscallGenerationTest, Wrapper) {
  ASSERT_EQ(zx_syscall_test_wrapper(1, 2, 3), 6, "syscall_test_wrapper doesn't add up");
  ASSERT_EQ(zx_syscall_test_wrapper(-1, 2, 3), ZX_ERR_INVALID_ARGS,
            "vdso should have checked args");
  ASSERT_EQ(zx_syscall_test_wrapper(10, 20, 30), ZX_ERR_OUT_OF_RANGE,
            "vdso should have checked the return");
}

TEST(SyscallGenerationTest, Syscall) {
  ASSERT_EQ(zx_syscall_test_8(1, 2, 3, 4, 5, 6, 7, 8), 36, "syscall8_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_7(1, 2, 3, 4, 5, 6, 7), 28, "syscall7_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_6(1, 2, 3, 4, 5, 6), 21, "syscall6_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_5(1, 2, 3, 4, 5), 15, "syscall5_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_4(1, 2, 3, 4), 10, "syscall4_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_3(1, 2, 3), 6, "syscall3_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_2(1, 2), 3, "syscall2_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_1(1), 1, "syscall1_test doesn't add up");
  ASSERT_EQ(zx_syscall_test_0(), 0, "syscall0_test doesn't add up");
}

TEST(SyscallGenerationTest, HandleCreateSuccess) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_syscall_test_handle_create(ZX_OK, &handle));

  EXPECT_NE(ZX_HANDLE_INVALID, handle);
  EXPECT_OK(zx_handle_close(handle));
}

TEST(SyscallGenerationTest, HandleCreateFailure) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_UNAVAILABLE, zx_syscall_test_handle_create(ZX_ERR_UNAVAILABLE, &handle));

  // Returning a non-OK status from the syscall should prevent the abigen
  // wrapper from copying handles out.
  EXPECT_EQ(ZX_HANDLE_INVALID, handle);
}

// zx_syscall_test_widening_* take four args of 64-bit, 32-bit, 16-bit, and
// 8-bit types, respectively.  The actual calling convention will use a 64-bit
// register for each of these arguments, with varying definitions per machine
// ABI about whose responsibility it is to zero-extend or sign-extend the low
// bits of the register.  So here we call each syscall as if its arguments were
// all full 64-bit values.  The kernel cannot safely assume anything about the
// high bits in argument registers for narrower-typed arguments.  So regardless
// of what the machine ABI says, we set extra high bits to ensure the kernel
// ignores them.  The *_narrow and *_wide syscalls differ in how the kernel's
// source code uses the values that the compiler could treat differently so as
// to cover more permutations of risky code generation possibilities.

using WidenedUnsignedArgs = uint64_t(uint64_t, uint64_t, uint64_t, uint64_t);
using WidenedSignedArgs = int64_t(int64_t, int64_t, int64_t, int64_t);

TEST(SyscallGenerationTest, WideningUnsignedNarrow) {
  const auto syscall = SyscallAs<WidenedUnsignedArgs>(&_zx_syscall_test_widening_unsigned_narrow);
  constexpr uint64_t k64 = (uint64_t{1} << 33) | 1;
  constexpr uint64_t k32 = (uint64_t{1} << 33) | 2;
  constexpr uint64_t k16 = (uint64_t{1} << 17) | 3;
  constexpr uint64_t k8 = (uint64_t{1} << 9) | 4;
  EXPECT_EQ(static_cast<uint64_t>(k64) + static_cast<uint32_t>(k32) +  //
                static_cast<uint16_t>(k16) + static_cast<uint8_t>(k8),
            syscall(k64, k32, k16, k8));
}

TEST(SyscallGenerationTest, WideningUnsignedWide) {
  const auto syscall = SyscallAs<WidenedUnsignedArgs>(&_zx_syscall_test_widening_unsigned_wide);
  constexpr uint64_t k64 = (uint64_t{1} << 33) | 1;
  constexpr uint64_t k32 = (uint64_t{1} << 33) | 2;
  constexpr uint64_t k16 = (uint64_t{1} << 17) | 3;
  constexpr uint64_t k8 = (uint64_t{1} << 9) | 4;
  EXPECT_EQ(static_cast<uint64_t>(k64) + static_cast<uint32_t>(k32) +  //
                static_cast<uint16_t>(k16) + static_cast<uint8_t>(k8),
            syscall(k64, k32, k16, k8));
}

TEST(SyscallGenerationTest, WideningSignedNarrow) {
  const auto syscall = SyscallAs<WidenedSignedArgs>(&_zx_syscall_test_widening_signed_narrow);
  constexpr int64_t k64s = -(int64_t{1} << 33);
  constexpr uint64_t k32s = (uint64_t{1} << 33) | cpp20::bit_cast<uint32_t>(int32_t{-2});
  constexpr uint64_t k16s = (uint64_t{1} << 17) | cpp20::bit_cast<uint16_t>(int16_t{-3});
  constexpr uint64_t k8s = (uint64_t{1} << 9) | cpp20::bit_cast<uint8_t>(int8_t{-4});
  EXPECT_EQ(static_cast<int64_t>(k64s) + static_cast<int32_t>(k32s) +  //
                static_cast<int16_t>(k16s) + static_cast<int8_t>(k8s),
            syscall(k64s, k32s, k16s, k8s));
}

TEST(SyscallGenerationTest, WideningSignedWide) {
  const auto syscall = SyscallAs<WidenedSignedArgs>(&_zx_syscall_test_widening_signed_wide);
  constexpr int64_t k64s = -(int64_t{1} << 33);
  constexpr uint64_t k32s = (uint64_t{1} << 33) | cpp20::bit_cast<uint32_t>(int32_t{-2});
  constexpr uint64_t k16s = (uint64_t{1} << 17) | cpp20::bit_cast<uint16_t>(int16_t{-3});
  constexpr uint64_t k8s = (uint64_t{1} << 9) | cpp20::bit_cast<uint8_t>(int8_t{-4});
  EXPECT_EQ(static_cast<int64_t>(k64s) + static_cast<int32_t>(k32s) +  //
                static_cast<int16_t>(k16s) + static_cast<int8_t>(k8s),
            syscall(k64s, k32s, k16s, k8s));
}

}  // namespace
