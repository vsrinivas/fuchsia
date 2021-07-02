// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "priv.h"

// zx_status_t zx_syscall_test_0
zx_status_t sys_syscall_test_0(void) { return 0; }
// zx_status_t zx_syscall_test_1
zx_status_t sys_syscall_test_1(int a) { return a; }
// zx_status_t zx_syscall_test_2
zx_status_t sys_syscall_test_2(int a, int b) { return a + b; }
// zx_status_t zx_syscall_test_3
zx_status_t sys_syscall_test_3(int a, int b, int c) { return a + b + c; }
// zx_status_t zx_syscall_test_4
zx_status_t sys_syscall_test_4(int a, int b, int c, int d) { return a + b + c + d; }
// zx_status_t zx_syscall_test_5
zx_status_t sys_syscall_test_5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
// zx_status_t zx_syscall_test_6
zx_status_t sys_syscall_test_6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}
// zx_status_t zx_syscall_test_7
zx_status_t sys_syscall_test_7(int a, int b, int c, int d, int e, int f, int g) {
  return a + b + c + d + e + f + g;
}
// zx_status_t zx_syscall_test_8
zx_status_t sys_syscall_test_8(int a, int b, int c, int d, int e, int f, int g, int h) {
  return a + b + c + d + e + f + g + h;
}
// zx_status_t zx_syscall_test_wrapper
zx_status_t sys_syscall_test_wrapper(int a, int b, int c) {
  if (a < 0 || b < 0 || c < 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  int ret = a + b + c;
  return (ret > 50 ? ZX_ERR_OUT_OF_RANGE : ret);
}

// zx_status_t zx_syscall_test_handle_create
//
// Unconditionally create a valid handle. If we return a non-OK status, the
// syscall wrappers should not copy the handle back to userspace.
zx_status_t sys_syscall_test_handle_create(zx_status_t return_value, user_out_handle* handle_out) {
  if (sys_event_create(0, handle_out) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  return return_value;
}

// If the compiler assumes that incoming high bits in argument registers for
// narrower-typed arguments are zero or sign-extended, then it won't narrow the
// arguments being passed from the syscall_test_* function to the Test*
// function and the high bits will show in the totals.

namespace {

[[gnu::noinline]] uint64_t TestNarrow(uint64_t a64, uint32_t a32, uint16_t a16, uint8_t a8) {
  return a64 + a32 + a16 + a8;
}

[[gnu::noinline]] int64_t TestSignedNarrow(int64_t a64, int32_t a32, int16_t a16, int8_t a8) {
  return a64 + a32 + a16 + a8;
}

[[gnu::noinline]] uint64_t TestWide(uint64_t a64, uint64_t a32, uint64_t a16, uint64_t a8) {
  return a64 + a32 + a16 + a8;
}

[[gnu::noinline]] int64_t TestSignedWide(int64_t a64, int64_t a32, int64_t a16, int64_t a8) {
  return a64 + a32 + a16 + a8;
}

}  // namespace

uint64_t sys_syscall_test_widening_unsigned_narrow(uint64_t a64, uint32_t a32, uint16_t a16,
                                                   uint8_t a8) {
  return TestNarrow(a64, a32, a16, a8);
}

uint64_t sys_syscall_test_widening_unsigned_wide(uint64_t a64, uint32_t a32, uint16_t a16,
                                                 uint8_t a8) {
  return TestWide(a64, a32, a16, a8);
}

int64_t sys_syscall_test_widening_signed_narrow(int64_t a64, int32_t a32, int16_t a16, int8_t a8) {
  return TestSignedNarrow(a64, a32, a16, a8);
}

int64_t sys_syscall_test_widening_signed_wide(int64_t a64, int32_t a32, int16_t a16, int8_t a8) {
  return TestSignedWide(a64, a32, a16, a8);
}
