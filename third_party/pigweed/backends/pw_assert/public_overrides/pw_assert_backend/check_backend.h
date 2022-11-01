// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_PIGWEED_BACKENDS_PW_ASSERT_PUBLIC_OVERRIDES_PW_ASSERT_BACKEND_CHECK_BACKEND_H_
#define THIRD_PARTY_PIGWEED_BACKENDS_PW_ASSERT_PUBLIC_OVERRIDES_PW_ASSERT_BACKEND_CHECK_BACKEND_H_

#include <zircon/assert.h>

#define PW_HANDLE_CRASH(fmt, ...) ZX_PANIC(fmt, ##__VA_ARGS__)

#define PW_HANDLE_ASSERT_FAILURE(x, msg, ...) \
  ZX_PANIC("ASSERT FAILED at (%s:%d): %s\n" msg, __FILE__, __LINE__, #x, ##__VA_ARGS__)

#define PW_HANDLE_ASSERT_BINARY_COMPARE_FAILURE(arg_a_str, arg_a_val, comparison_op_str,  \
                                                arg_b_str, arg_b_val, type_fmt, msg, ...) \
  ZX_PANIC("ASSERT FAILED at (%s:%d): " arg_a_str " (=" type_fmt ") " comparison_op_str   \
           " " arg_b_str " (=" type_fmt ") \n" msg,                                  \
           __FILE__, __LINE__, arg_a_val, arg_b_val, ##__VA_ARGS__)

#endif  // THIRD_PARTY_PIGWEED_BACKENDS_PW_ASSERT_PUBLIC_OVERRIDES_PW_ASSERT_BACKEND_CHECK_BACKEND_H_
