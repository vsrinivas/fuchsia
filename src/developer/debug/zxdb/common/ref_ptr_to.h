// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_REF_PTR_TO_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_REF_PTR_TO_H_

#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

// Converts a ray pointer to a reference-counted type to a RefPtr.
//
// Const and fxl::RefPtr don't play well together so this requires an awkward const_cast. Callers
// should take care to preserve the const expectation of the caller.
//
// In zxdb this is normally this is done for Symbol-derived classes which are const in their normal
// usage anyway.
//
// Example:
//
//    void Foo(const Type* type) {
//      fxl::RefPtr<Type> ref = RefPtrTo(type);
//      ...
//    }
template <typename T>
fxl::RefPtr<T> RefPtrTo(const T* pointer) {
  return fxl::RefPtr<T>(const_cast<T*>(pointer));
}

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_REF_PTR_TO_H_
