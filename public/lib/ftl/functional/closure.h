// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FUNCTIONAL_CLOSURE_H_
#define LIB_FTL_FUNCTIONAL_CLOSURE_H_

#include <functional>

namespace ftl {

typedef std::function<void()> Closure;

}  // namespace ftl

#endif  // LIB_FTL_FUNCTIONAL_CLOSURE_H_
