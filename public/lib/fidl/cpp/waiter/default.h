// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WAITER_DEFAULT_H_
#define LIB_FIDL_CPP_WAITER_DEFAULT_H_

#include "lib/fidl/c/waiter/async_waiter.h"

namespace fidl {

const FidlAsyncWaiter* GetDefaultAsyncWaiter();

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WAITER_DEFAULT_H_
