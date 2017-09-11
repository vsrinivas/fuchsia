// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Macros for static thread-safety analysis.
//
// These are from http://clang.llvm.org/docs/ThreadSafetyAnalysis.html (and thus
// really derive from google3's thread_annotations.h).
//
// TODO(vtl): We're still using the old-fashioned, deprecated annotations
// ("locks" instead of "capabilities"), since the new ones don't work yet (in
// particular, |TRY_ACQUIRE()| doesn't work: b/19264527).
// https://github.com/domokit/mojo/issues/314

#ifndef LIB_FXL_SYNCHRONIZATION_THREAD_ANNOTATIONS_H_
#define LIB_FXL_SYNCHRONIZATION_THREAD_ANNOTATIONS_H_

// Enable thread-safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.
#if defined(__clang__)
#define FXL_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define FXL_THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

#define FXL_GUARDED_BY(x) FXL_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define FXL_PT_GUARDED_BY(x) FXL_THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define FXL_ACQUIRE(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define FXL_ACQUIRED_AFTER(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define FXL_ACQUIRED_BEFORE(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define FXL_EXCLUSIVE_LOCKS_REQUIRED(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(exclusive_locks_required(__VA_ARGS__))

#define FXL_SHARED_LOCKS_REQUIRED(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(shared_locks_required(__VA_ARGS__))

#define FXL_LOCKS_EXCLUDED(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define FXL_LOCK_RETURNED(x) FXL_THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define FXL_LOCKABLE FXL_THREAD_ANNOTATION_ATTRIBUTE__(lockable)

#define FXL_SCOPED_LOCKABLE FXL_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define FXL_EXCLUSIVE_LOCK_FUNCTION(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(exclusive_lock_function(__VA_ARGS__))

#define FXL_SHARED_LOCK_FUNCTION(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(shared_lock_function(__VA_ARGS__))

#define FXL_ASSERT_EXCLUSIVE_LOCK(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(assert_exclusive_lock(__VA_ARGS__))

#define FXL_ASSERT_SHARED_LOCK(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_lock(__VA_ARGS__))

#define FXL_EXCLUSIVE_TRYLOCK_FUNCTION(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(exclusive_trylock_function(__VA_ARGS__))

#define FXL_SHARED_TRYLOCK_FUNCTION(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(shared_trylock_function(__VA_ARGS__))

#define FXL_UNLOCK_FUNCTION(...) \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(unlock_function(__VA_ARGS__))

#define FXL_NO_THREAD_SAFETY_ANALYSIS \
  FXL_THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// Use this in the header to annotate a function/method as not being
// thread-safe. This is equivalent to |FXL_NO_THREAD_SAFETY_ANALYSIS|, but
// semantically different: it declares that the caller must abide by additional
// restrictions. Limitation: Unfortunately, you can't apply this to a method in
// an interface (i.e., pure virtual method) and have it applied automatically to
// implementations.
#define FXL_NOT_THREAD_SAFE FXL_NO_THREAD_SAFETY_ANALYSIS

#endif  // LIB_FXL_SYNCHRONIZATION_THREAD_ANNOTATIONS_H_
