// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_AUTO_CALL_H_
#define FBL_AUTO_CALL_H_

#include <lib/fit/defer.h>

namespace fbl {

// See fit::deferred_action and fit::defer.
//
// TODO(fxbug.dev/57355): Delete aliases once usage has been migrated over to
// fit explicitly.
template <typename T>
using AutoCall = fit::deferred_action<T>;

template <typename T>
inline AutoCall<T> MakeAutoCall(T c) {
  return fit::defer(std::move(c));
}

}  // namespace fbl

#endif  // FBL_AUTO_CALL_H_
