// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_ASSERT_
#define SYSROOT_ZIRCON_ASSERT_

// For a description of which asserts are enabled at which debug levels, see the documentation for
// GN build argument |assert_level|.

#ifdef _KERNEL
#include <assert.h>
#define ZX_PANIC(args...) PANIC(args)
#define ZX_ASSERT(args...) ASSERT(args)
#define ZX_ASSERT_MSG(args...) ASSERT_MSG(args)
#define ZX_DEBUG_ASSERT(args...) DEBUG_ASSERT(args)
#define ZX_DEBUG_ASSERT_MSG(args...) DEBUG_ASSERT_MSG(args)
#define ZX_DEBUG_ASSERT_COND(args...) DEBUG_ASSERT_COND(args)
#define ZX_DEBUG_ASSERT_MSG_COND(args...) DEBUG_ASSERT_MSG_COND(args)
#define ZX_DEBUG_ASSERT_IMPLEMENTED DEBUG_ASSERT_IMPLEMENTED

#else  // #ifdef _KERNEL

#include <zircon/compiler.h>

__BEGIN_CDECLS
void __zx_panic(const char* format, ...) __NO_RETURN __PRINTFLIKE(1, 2);
__END_CDECLS

#define ZX_PANIC(fmt, ...) __zx_panic((fmt), ##__VA_ARGS__)

// Assert that |x| is true, else panic.
//
// ZX_ASSERT is always enabled and |x| will be evaluated regardless of any build arguments.
#define ZX_ASSERT(x)                                                      \
  do {                                                                    \
    if (unlikely(!(x))) {                                                 \
      ZX_PANIC("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                     \
  } while (0)

// Assert that |x| is true, else panic with the given message.
//
// ZX_ASSERT_MSG is always enabled and |x| will be evaluated regardless of any build arguments.
#define ZX_ASSERT_MSG(x, msg, msgargs...)                                                     \
  do {                                                                                        \
    if (unlikely(!(x))) {                                                                     \
      ZX_PANIC("ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ##msgargs); \
    }                                                                                         \
  } while (0)

// Conditionally implement ZX_DEBUG_ASSERT based on ZX_ASSERT_LEVEL.
#ifdef ZX_ASSERT_LEVEL

// ZX_DEBUG_ASSERT_IMPLEMENTED is intended to be used to conditionalize code that is logically part
// of a debug assert. It's useful for performing complex consistency checks that are difficult to
// work into a ZX_DEBUG_ASSERT statement.
#define ZX_DEBUG_ASSERT_IMPLEMENTED (ZX_ASSERT_LEVEL > 1)
#else
#define ZX_DEBUG_ASSERT_IMPLEMENTED 0
#endif

// Assert that |x| is true, else panic.
//
// Depending on build arguments, ZX_DEBUG_ASSERT may or may not be enabled. When disabled, |x| will
// not be evaluated.
#define ZX_DEBUG_ASSERT(x)                                                      \
  do {                                                                          \
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                        \
      ZX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                           \
  } while (0)

// Assert that |x| is true, else panic with the given message.
//
// Depending on build arguments, ZX_DEBUG_ASSERT_MSG may or may not be enabled. When disabled, |x|
// will not be evaluated.
#define ZX_DEBUG_ASSERT_MSG(x, msg, msgargs...)                                         \
  do {                                                                                  \
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                                \
      ZX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, \
               ##msgargs);                                                              \
    }                                                                                   \
  } while (0)

// implement _COND versions of ZX_DEBUG_ASSERT which only emit the body if
// ZX_DEBUG_ASSERT_IMPLEMENTED is set
#if ZX_DEBUG_ASSERT_IMPLEMENTED
#define ZX_DEBUG_ASSERT_COND(x) ZX_DEBUG_ASSERT(x)
#define ZX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) ZX_DEBUG_ASSERT_MSG(x, msg, msgargs)
#else
#define ZX_DEBUG_ASSERT_COND(x) \
  do {                          \
  } while (0)
#define ZX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) \
  do {                                               \
  } while (0)
#endif
#endif  // #ifdef _KERNEL

#endif  // SYSROOT_ZIRCON_ASSERT_
