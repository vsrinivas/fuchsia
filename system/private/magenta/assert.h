// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

#if _KERNEL
#include <debug.h>
#define PANIC(fmt, args...) panic(fmt, args)
#else  // if _KERNEL
__BEGIN_CDECLS
extern int printf(const char*, ...) __PRINTFLIKE(1, 2);
extern void abort(void);
__END_CDECLS
#define PANIC(fmt, ...)              \
    do {                             \
        printf(fmt, ##__VA_ARGS__);  \
        abort();                     \
    } while (0)
#endif  // if _KERNEL

#define ASSERT(x) \
    do {                                                                                          \
        if (unlikely(!(x))) {                                                                     \
            PANIC("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x);                      \
        }                                                                                         \
    } while (0)

#define ASSERT_MSG(x, msg, msgargs...)                                                            \
    do {                                                                                          \
        if (unlikely(!(x))) {                                                                     \
            PANIC("ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ## msgargs); \
        }                                                                                         \
    } while (0)

#if (LK_DEBUGLEVEL > 1)

#define DEBUG_ASSERT(x) \
    do {                                                                           \
        if (unlikely(!(x))) {                                                      \
            PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
        }                                                                          \
    } while (0)

#define DEBUG_ASSERT_MSG(x, msg, msgargs...)                        \
    do {                                                            \
        if (unlikely(!(x))) {                                       \
            PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n" msg "\n",  \
                  __FILE__, __LINE__, #x, ## msgargs);              \
        }                                                           \
    } while (0)

#else  // if (LK_DEBUGLEVEL > 1)

#define DEBUG_ASSERT(x) do { } while(0)
#define DEBUG_ASSERT_MSG(x, msg, msgargs...) do { } while(0)
#endif  // if (LK_DEBUGLEVEL > 1)

#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert(e, msg) _Static_assert(e, msg)
#endif
