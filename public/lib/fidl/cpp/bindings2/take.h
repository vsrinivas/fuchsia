// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_TAKE_H_
#define LIB_FIDL_CPP_BINDINGS2_TAKE_H_

#include <fidl/builder.h>

#include "lib/fidl/cpp/bindings2/traits.h"

namespace fidl {

// Copies the value from |*view| to |*value|.
template<typename T>
inline typename std::enable_if<IsPrimitive<T>::value, void>::type
Take(T* value, T* view) {
  *value = *view;
}

// Moves the handle from |*view| to |*object|.
//
// After this function returns, |*view| is ZX_HANDLE_INVALID and the ownership
// of the handle that was previously stored in |*view| (if any) has been
// transferred to |*object|.
template<typename T>
void Take(zx::object<T>* object, zx_handle_t* view) {
  object->reset(*view);
  *view = ZX_HANDLE_INVALID;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_TAKE_H_
