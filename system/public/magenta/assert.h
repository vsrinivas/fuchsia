// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef _KERNEL
#include <assert.h>
#define MX_PANIC(args...) PANIC(args)
#define MX_ASSERT(args...) ASSERT(args)
#define MX_ASSERT_MSG(args...) ASSERT_MSG(args)
#define MX_DEBUG_ASSERT(args...) DEBUG_ASSERT(args)
#define MX_DEBUG_ASSERT_MSG(args...) DEBUG_ASSERT_MSG(args)
#define MX_DEBUG_ASSERT_COND(args...) DEBUG_ASSERT_COND(args)
#define MX_DEBUG_ASSERT_MSG_COND(args...) DEBUG_ASSERT_MSG_COND(args)
#define MX_DEBUG_ASSERT_IMPLEMENTED DEBUG_ASSERT_IMPLEMENTED

#ifdef MX_DEBUGLEVEL
#undef MX_DEBUGLEVEL
#endif
#define MX_DEBUGLEVEL LK_DEBUGLEVEL

#else // #ifdef _KERNEL

#include <stdio.h> // for printf
#include <stdlib.h> // for abort

#include <magenta/compiler.h>

#define MX_PANIC(fmt, ...)          \
    do {                            \
        printf(fmt, ##__VA_ARGS__); \
        abort();                    \
    } while (0)

#define MX_ASSERT(x)                                                            \
    do {                                                                        \
        if (unlikely(!(x))) {                                                   \
            MX_PANIC("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
        }                                                                       \
    } while (0)

#define MX_ASSERT_MSG(x, msg, msgargs...)                                                           \
    do {                                                                                            \
        if (unlikely(!(x))) {                                                                       \
            MX_PANIC("ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ##msgargs); \
        }                                                                                           \
    } while (0)

// conditionally implement DEBUG_ASSERT based on MX_DEBUGLEVEL in kernel space
// user space does not currently implement DEBUG_ASSERT
#ifdef MX_DEBUGLEVEL
#define MX_DEBUG_ASSERT_IMPLEMENTED (MX_DEBUGLEVEL > 1)
#else
#define MX_DEBUG_ASSERT_IMPLEMENTED 0
#endif

#define MX_DEBUG_ASSERT(x)                                                            \
    do {                                                                              \
        if (MX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                          \
            MX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
        }                                                                             \
    } while (0)

#define MX_DEBUG_ASSERT_MSG(x, msg, msgargs...)                       \
    do {                                                              \
        if (MX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {          \
            MX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n" msg "\n", \
                     __FILE__, __LINE__, #x, ##msgargs);              \
        }                                                             \
    } while (0)

// implement _COND versions of MX_DEBUG_ASSERT which only emit the body if
// MX_DEBUG_ASSERT_IMPLEMENTED is set
#if MX_DEBUG_ASSERT_IMPLEMENTED
#define MX_DEBUG_ASSERT_COND(x) MX_DEBUG_ASSERT(x)
#define MX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) MX_DEBUG_ASSERT_MSG(x, msg, msgargs)
#else
#define MX_DEBUG_ASSERT_COND(x) do { } while (0)
#define MX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) do { } while (0)
#endif
#endif // #ifdef _KERNEL
