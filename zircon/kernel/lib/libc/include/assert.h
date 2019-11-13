// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ASSERT_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ASSERT_H_

// For a description of which asserts are enabled at which debug levels, see the documentation for
// GN build argument |assert_level|.

#include <debug.h>
#include <zircon/compiler.h>

#define PANIC(args...) panic(args)

// Assert that |x| is true, else panic.
//
// ASSERT is always enabled and |x| will be evaluated regardless of any build arguments.
#define ASSERT(x)                                                      \
  do {                                                                 \
    if (unlikely(!(x))) {                                              \
      PANIC("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                  \
  } while (0)

// Assert that |x| is true, else panic with the given message.
//
// ASSERT_MSG is always enabled and |x| will be evaluated regardless of any build arguments.
#define ASSERT_MSG(x, msg, msgargs...)                                                     \
  do {                                                                                     \
    if (unlikely(!(x))) {                                                                  \
      PANIC("ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ##msgargs); \
    }                                                                                      \
  } while (0)

// Conditionally implement DEBUG_ASSERT based on LK_DEBUGLEVEL in kernel space.
#ifdef LK_DEBUGLEVEL
#define DEBUG_ASSERT_IMPLEMENTED (LK_DEBUGLEVEL > 1)
#else
#define DEBUG_ASSERT_IMPLEMENTED 0
#endif

// Assert that |x| is true, else panic.
//
// Depending on build arguments, DEBUG_ASSERT may or may not be enabled. When disabled, |x| will not
// be evaluated.
#define DEBUG_ASSERT(x)                                                      \
  do {                                                                       \
    if (DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                        \
      PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                        \
  } while (0)

// Assert that |x| is true, else panic with the given message.
//
// Depending on build arguments, DEBUG_ASSERT_MSG may or may not be enabled. When disabled, |x| will
// not be evaluated.
#define DEBUG_ASSERT_MSG(x, msg, msgargs...)                                                     \
  do {                                                                                           \
    if (DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                                            \
      PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ##msgargs); \
    }                                                                                            \
  } while (0)

// implement _COND versions of DEBUG_ASSERT which only emit the body if
// DEBUG_ASSERT_IMPLEMENTED is set
#if DEBUG_ASSERT_IMPLEMENTED
#define DEBUG_ASSERT_COND(x) DEBUG_ASSERT(x)
#define DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) DEBUG_ASSERT_MSG(x, msg, msgargs)
#else
#define DEBUG_ASSERT_COND(x) \
  do {                       \
  } while (0)
#define DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) \
  do {                                            \
  } while (0)
#endif

// make sure static_assert() is defined, even in C
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert(e, msg) _Static_assert(e, msg)
#endif

// Use DEBUG_ASSERT or ASSERT instead.
//
// assert() is defined only because third-party code used in the kernel expects it.
#define assert(x) DEBUG_ASSERT(x)

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ASSERT_H_
