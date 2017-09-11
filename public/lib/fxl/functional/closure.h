// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FUNCTIONAL_CLOSURE_H_
#define LIB_FXL_FUNCTIONAL_CLOSURE_H_

#include <functional>

namespace fxl {

typedef std::function<void()> Closure;

}  // namespace fxl

#endif  // LIB_FXL_FUNCTIONAL_CLOSURE_H_
