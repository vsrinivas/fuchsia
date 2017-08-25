// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __ASSEMBLY__

#if __GNUC__ || defined(__clang__)
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define __UNUSED __attribute__((__unused__))
#define __USED __attribute__((__used__))
#define __PACKED __attribute__((packed))
#define __ALIGNED(x) __attribute__((aligned(x)))
#define __PRINTFLIKE(__fmt,__varargs) __attribute__((__format__ (__printf__, __fmt, __varargs)))
#define __SCANFLIKE(__fmt,__varargs) __attribute__((__format__ (__scanf__, __fmt, __varargs)))
#define __SECTION(x) __attribute__((__section__(x)))
#define __PURE __attribute__((__pure__))
#define __CONST __attribute__((__const__))
#define __NO_RETURN __attribute__((__noreturn__))
#define __MALLOC __attribute__((__malloc__))
#define __WEAK __attribute__((__weak__))
#define __GNU_INLINE __attribute__((__gnu_inline__))
#define __GET_CALLER(x) __builtin_return_address(0)
#define __GET_FRAME(x) __builtin_frame_address(0)
#define __NAKED __attribute__((__naked__))
#define __ISCONSTANT(x) __builtin_constant_p(x)
#define __NO_INLINE __attribute__((__noinline__))
#define __SRAM __NO_INLINE __SECTION(".sram.text")
#define __CONSTRUCTOR __attribute__((__constructor__))
#define __DESTRUCTOR __attribute__((__destructor__))

#ifndef __clang__
#define __LEAF_FN __attribute__((__leaf__))
#define __OPTIMIZE(x) __attribute__((__optimize__(x)))
#define __EXTERNALLY_VISIBLE __attribute__((__externally_visible__))
#define __THREAD_ANNOTATION(x)
#define __NO_SAFESTACK
#define __has_feature(x) 0
#else
#define __LEAF_FN
#define __OPTIMIZE(x)
#define __EXTERNALLY_VISIBLE
#ifndef DISABLE_THREAD_ANNOTATIONS
#define __THREAD_ANNOTATION(x) __attribute__((x))
#else
#define __THREAD_ANNOTATION(x)
#endif
#define __NO_SAFESTACK __attribute__((__no_sanitize__("safe-stack")))
#endif

#define __ALWAYS_INLINE __attribute__((__always_inline__))
#define __MAY_ALIAS __attribute__((__may_alias__))
#define __NONNULL(x) __attribute__((__nonnull__ x))
#define __WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#define __UNREACHABLE __builtin_unreachable()
#define __WEAK_ALIAS(x) __attribute__((__weak__, __alias__(x)))
#define __ALIAS(x) __attribute__((__alias__(x)))
#define __EXPORT __attribute__ ((__visibility__("default")))
#define __LOCAL  __attribute__ ((__visibility__("hidden")))
#define __THREAD __thread
#define __offsetof(type, field) __builtin_offsetof(type, field)

// Publicly exposed thread annotation macros. These have a long and ugly name to
// minimize the chance of collision with consumers of Magenta's public headers.
#define __TA_CAPABILITY(x) __THREAD_ANNOTATION(__capability__(x))
#define __TA_GUARDED(x) __THREAD_ANNOTATION(__guarded_by__(x))
#define __TA_ACQUIRE(...) __THREAD_ANNOTATION(__acquire_capability__(__VA_ARGS__))
#define __TA_ACQUIRED_BEFORE(...) __THREAD_ANNOTATION(__acquired_before__(__VA_ARGS__))
#define __TA_ACQUIRED_AFTER(...) __THREAD_ANNOTATION(__acquired_after__(__VA_ARGS__))
#define __TA_RELEASE(...) __THREAD_ANNOTATION(__release_capability__(__VA_ARGS__))
#define __TA_REQUIRES(...) __THREAD_ANNOTATION(__requires_capability__(__VA_ARGS__))
#define __TA_EXCLUDES(...) __THREAD_ANNOTATION(__locks_excluded__(__VA_ARGS__))
#define __TA_RETURN_CAPABILITY(x) __THREAD_ANNOTATION(__lock_returned__(x))
#define __TA_SCOPED_CAPABILITY __THREAD_ANNOTATION(__scoped_lockable__)
#define __TA_NO_THREAD_SAFETY_ANALYSIS __THREAD_ANNOTATION(__no_thread_safety_analysis__)

#if !defined __DEPRECATED
#define __DEPRECATED __attribute__((__deprecated__))
#endif

#else  // if __GNUC__ || defined(__clang__)

#warning "Unrecognized compiler!  Please update global/include/compiler.h"

#define likely(x)
#define unlikely(x)
#define __UNUSED
#define __USED
#define __PACKED
#define __ALIGNED(x)
#define __PRINTFLIKE(__fmt,__varargs)
#define __SCANFLIKE(__fmt,__varargs)
#define __SECTION(x)
#define __PURE
#define __LEAF_FN
#define __CONST
#define __NO_RETURN
#define __MALLOC
#define __WEAK
#define __GNU_INLINE
#define __GET_CALLER(x)
#define __GET_FRAME(x)
#define __NAKED
#define __ISCONSTANT(x)
#define __NO_INLINE
#define __SRAM
#define __CONSTRUCTOR
#define __DESTRUCTOR
#define __OPTIMIZE(x)
#define __ALWAYS_INLINE
#define __MAY_ALIAS
#define __NONNULL(x)
#define __WARN_UNUSED_RESULT
#define __EXTERNALLY_VISIBLE
#define __UNREACHABLE
#define __WEAK_ALIAS(x)
#define __ALIAS(x)
#define __EXPORT
#define __LOCAL
#define __THREAD

#if !defined __DEPRECATED
#define __DEPRECATED
#endif

#endif  // if __GNUC__ || defined(__clang__)
#endif  // ifndef __ASSEMBLY__

/* TODO: add type check */
#define countof(a) (sizeof(a) / sizeof((a)[0]))

/* CPP header guards */
#ifdef __cplusplus
#define __BEGIN_CDECLS  extern "C" {
#define __END_CDECLS    }
#else
#define __BEGIN_CDECLS
#define __END_CDECLS
#endif

// constexpr annotation for use in static inlines usable in both C and C++
#ifdef __cplusplus
#define __CONSTEXPR constexpr
#else
#define __CONSTEXPR
#endif
