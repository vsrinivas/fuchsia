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
#define __SECTION(x) __attribute((section(x)))
#define __PURE __attribute((pure))
#define __CONST __attribute((const))
#define __NO_RETURN __attribute__((noreturn))
#define __MALLOC __attribute__((malloc))
#define __WEAK __attribute__((weak))
#define __GNU_INLINE __attribute__((gnu_inline))
#define __GET_CALLER(x) __builtin_return_address(0)
#define __GET_FRAME(x) __builtin_frame_address(0)
#define __NAKED __attribute__((naked))
#define __ISCONSTANT(x) __builtin_constant_p(x)
#define __NO_INLINE __attribute((noinline))
#define __SRAM __NO_INLINE __SECTION(".sram.text")
#define __CONSTRUCTOR __attribute__((constructor))
#define __DESTRUCTOR __attribute__((destructor))

#ifndef __clang__
#define __OPTIMIZE(x) __attribute__((optimize(x)))
#define __EXTERNALLY_VISIBLE __attribute__((externally_visible))
#define __THREAD_ANNOTATION(x)
#define __NO_SAFESTACK
#define __has_feature(x) 0
#else
#define __OPTIMIZE(x)
#define __EXTERNALLY_VISIBLE
#define __THREAD_ANNOTATION(x) __attribute__((x))
#define __NO_SAFESTACK __attribute__((no_sanitize("safe-stack")))
#endif

#define __ALWAYS_INLINE __attribute__((always_inline))
#define __MAY_ALIAS __attribute__((may_alias))
#define __NONNULL(x) __attribute((nonnull x))
#define __WARN_UNUSED_RESULT __attribute((warn_unused_result))
#define __UNREACHABLE __builtin_unreachable()
#define __WEAK_ALIAS(x) __attribute__((weak, alias(x)))
#define __ALIAS(x) __attribute__((alias(x)))
#define __EXPORT __attribute__ ((visibility("default")))
#define __LOCAL  __attribute__ ((visibility("hidden")))
#define __THREAD __thread
#define __offsetof(type, field) __builtin_offsetof(type, field)

// Publicly exposed thread annotation macros. These have a long and ugly name to
// minimize the chance of collision with consumers of Magenta's public headers.
#define __TA_CAPABILITY(x) __THREAD_ANNOTATION(capability(x))
#define __TA_GUARDED(x) __THREAD_ANNOTATION(guarded_by(x))
#define __TA_ACQUIRE(...) __THREAD_ANNOTATION(acquire_capability(__VA_ARGS__))
#define __TA_ACQUIRED_BEFORE(...) __THREAD_ANNOTATION(acquired_before(__VA_ARGS__))
#define __TA_ACQUIRED_AFTER(...) __THREAD_ANNOTATION(acquired_after(__VA_ARGS__))
#define __TA_RELEASE(...) __THREAD_ANNOTATION(release_capability(__VA_ARGS__))
#define __TA_REQUIRES(...) __THREAD_ANNOTATION(requires_capability(__VA_ARGS__))
#define __TA_EXCLUDES(...) __THREAD_ANNOTATION(locks_excluded(__VA_ARGS__))
#define __TA_RETURN_CAPABILITY(x) __THREAD_ANNOTATION(lock_returned(x))
#define __TA_SCOPED_CAPABILITY __THREAD_ANNOTATION(scoped_lockable)
#define __TA_NO_THREAD_SAFETY_ANALYSIS __THREAD_ANNOTATION(no_thread_safety_analysis)

#if !defined __DEPRECATED
#define __DEPRECATED __attribute((deprecated))
#endif

/* compiler fence */
#define CF do { __asm__ volatile("" ::: "memory"); } while(0)

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

#define CF

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
