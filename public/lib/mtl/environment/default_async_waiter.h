// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_ENVIRONMENT_DEFAULT_ASYNC_WAITER_H_
#define LIB_MTL_ENVIRONMENT_DEFAULT_ASYNC_WAITER_H_

struct MojoAsyncWaiter;

namespace mtl {
namespace internal {

extern const MojoAsyncWaiter kDefaultAsyncWaiter;

}  // namespace internal
}  // namespace mtl

#endif  // LIB_MTL_ENVIRONMENT_DEFAULT_ASYNC_WAITER_H_
