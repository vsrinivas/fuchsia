// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_THREAD_SAFETY_H_
#define LIB_FIT_THREAD_SAFETY_H_

// Thread-safety annotations.
// Currently these are only supported on Clang.
#if defined(__clang__) && defined(_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS) && \
    __has_attribute(acquire_capability)
#define FIT_THREAD_ANNOTATION(x) __attribute__((x))
#else
#define FIT_THREAD_ANNOTATION(x)
#endif
#define FIT_CAPABILITY(x) FIT_THREAD_ANNOTATION(__capability__(x))
#define FIT_GUARDED(x) FIT_THREAD_ANNOTATION(__guarded_by__(x))
#define FIT_ACQUIRE(...) FIT_THREAD_ANNOTATION(__acquire_capability__(__VA_ARGS__))
#define FIT_TRY_ACQUIRE(...) FIT_THREAD_ANNOTATION(__try_acquire_capability__(__VA_ARGS__))
#define FIT_ACQUIRED_BEFORE(...) FIT_THREAD_ANNOTATION(__acquired_before__(__VA_ARGS__))
#define FIT_ACQUIRED_AFTER(...) FIT_THREAD_ANNOTATION(__acquired_after__(__VA_ARGS__))
#define FIT_RELEASE(...) FIT_THREAD_ANNOTATION(__release_capability__(__VA_ARGS__))
#define FIT_REQUIRES(...) FIT_THREAD_ANNOTATION(__requires_capability__(__VA_ARGS__))
#define FIT_EXCLUDES(...) FIT_THREAD_ANNOTATION(__locks_excluded__(__VA_ARGS__))
#define FIT_RETURN_CAPABILITY(x) FIT_THREAD_ANNOTATION(__lock_returned__(x))
#define FIT_SCOPED_CAPABILITY FIT_THREAD_ANNOTATION(__scoped_lockable__)
#define FIT_NO_THREAD_SAFETY_ANALYSIS FIT_THREAD_ANNOTATION(__no_thread_safety_analysis__)

#endif  // LIB_FIT_THREAD_SAFETY_H_
