// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_PUT_H_
#define LIB_FIDL_CPP_BINDINGS2_PUT_H_

#include <fidl/cpp/builder.h>

#include "lib/fidl/cpp/bindings2/traits.h"

namespace fidl {

// Copies |*value| to |*view| and returns true.
//
// The |builder| argument is ignored but accepted to make it easier to generate
// code that calls this function.
template<typename T>
inline typename std::enable_if<IsPrimitive<T>::value, bool>::type
PutAt(Builder* builder, T* view, T* value) {
  *view = *value;
  return true;
}

// Transfers ownership of |*object| to |*view| and returns true.
//
// After this function returns |object->get()| is ZX_HANDLE_INVALID and the ownership
// of the handle that was previously stored in |*object| (if any) has been
// transferred to |*view|.
//
// The |builder| argument is ignored but accepted to make it easier to generate
// code that calls this function.
bool PutAt(Builder* builder, zx_handle_t* view, zx::object_base* object);

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_PUT_H_
