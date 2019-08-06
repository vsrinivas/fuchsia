// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

TEST(AbigenTest, Wrapper) {
  ASSERT_EQ(zx_syscall_test_wrapper(1, 2, 3), 6, "syscall_test_wrapper doesn't add up");
  ASSERT_EQ(zx_syscall_test_wrapper(-1, 2, 3), ZX_ERR_INVALID_ARGS,
            "vdso should have checked args");
  ASSERT_EQ(zx_syscall_test_wrapper(10, 20, 30), ZX_ERR_OUT_OF_RANGE,
            "vdso should have checked the return");
}

TEST(AbigenTest, Syscall) {
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

TEST(AbigenTest, HandleCreateSuccess) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_syscall_test_handle_create(ZX_OK, &handle));

  EXPECT_NE(ZX_HANDLE_INVALID, handle);
  EXPECT_OK(zx_handle_close(handle));
}

TEST(AbigenTest, HandleCreateFailure) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_UNAVAILABLE, zx_syscall_test_handle_create(ZX_ERR_UNAVAILABLE, &handle));

  // Returning a non-OK status from the syscall should prevent the abigen
  // wrapper from copying handles out.
  EXPECT_EQ(ZX_HANDLE_INVALID, handle);
}

}  // namespace
