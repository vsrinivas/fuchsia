// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_MEMORY_WEAK_PTR_INTERNAL_H_
#define LIB_FTL_MEMORY_WEAK_PTR_INTERNAL_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace ftl {
namespace internal {

// |WeakPtr<T>|s have a reference to a |WeakPtrFlag| to determine whether they
// are valid (non-null) or not. We do not store a |T*| in this object since
// there may also be |WeakPtr<U>|s to the same object, where |U| is a superclass
// of |T|.
//
// This class in not thread-safe, though references may be released on any
// thread (allowing weak pointers to be destroyed/reset/reassigned on any
// thread).
class WeakPtrFlag : public RefCountedThreadSafe<WeakPtrFlag> {
 public:
  WeakPtrFlag();
  ~WeakPtrFlag();

  bool is_valid() const { return is_valid_; }

  void Invalidate();

 private:
  bool is_valid_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WeakPtrFlag);
};

}  // namespace internal
}  // namespace ftl

#endif  // LIB_FTL_MEMORY_WEAK_PTR_INTERNAL_H_
