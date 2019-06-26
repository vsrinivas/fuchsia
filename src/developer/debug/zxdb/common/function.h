// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_FUNCTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_FUNCTION_H_

#include <functional>

#include "lib/fit/function.h"

namespace zxdb {

// Converts a fit::callback<...> to a std::function<...>.
//
// Since a fit::callback can only be called once, the returned std::function
// must also be called at most once.
//
// TODO(brettw) This function is a temporary stopgap while std::function is
// converted to fit::callback in our codebase. When the conversion is complete,
// this should be deleted.
template <typename Result, typename... Args>
std::function<Result(Args...)> FitCallbackToStdFunction(fit::callback<Result(Args...)> f) {
  // Moves the fit::function to a heap-allocated shared_ptr which allows
  // copying without copying the underlying fit::callback.
  return std::function<Result(Args...)>(
      [fn = std::make_shared<fit::callback<Result(Args...)>>(std::move(f))](Args&&... args) {
        return (*fn)(std::forward<Args>(args)...);
      });
}

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_FUNCTION_H_
