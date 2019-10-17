// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

// Return static array size as const value.
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

// Function attribute to use to indicate that a function never returns.
// To be placed at the end of function *declarations* only.
//
#if defined(__GCC__) || defined(__clang__)
#define FUNC_ATTRIBUTE_NORETURN __attribute__((noreturn))
#else
#define FUNC_ATTRIBUTE_NORETURN /* nothing */
#endif

// Do not call these directly, see ASSERT, ASSERT_MSG, etc below.
extern void
assert_panic_(const char * file, int line, const char * fmt, ...) FUNC_ATTRIBUTE_NORETURN;

// Panic immediately with a trivial error message if |condition| is not true.
// Use ASSERT_MSG() if you want to provide your own message instead.
#define ASSERT(condition)                                                                          \
  do                                                                                               \
    {                                                                                              \
      if (!(condition))                                                                            \
        assert_panic_(__FILE__, __LINE__, #condition);                                             \
    }                                                                                              \
  while (0)

// Panic immediately if |condition| is not true, passing a formatted message
// in this case.
#define ASSERT_MSG(condition, ...)                                                                 \
  do                                                                                               \
    {                                                                                              \
      if (!(condition))                                                                            \
        assert_panic_(__FILE__, __LINE__, __VA_ARGS__);                                            \
    }                                                                                              \
  while (0)

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_UTILS_H_
