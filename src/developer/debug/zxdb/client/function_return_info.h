// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_RETURN_INFO_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_RETURN_INFO_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"

namespace zxdb {

class Thread;

// Identifies the state of the function return.
struct FunctionReturnInfo {
  // Guaranteed non-null for all notifications.
  Thread* thread = nullptr;

  // The symbol for the function that just completed. This won't be valid if the function stepped
  // out of an unsymbolized function.
  LazySymbol symbol;
};

// This callback type is used by thread controllers to notify their clients that a physical
// (non-inline) function call has just returned. It will be issued on the instruction immediately
// following the return and the thread will be stopped.
using FunctionReturnCallback = fit::callback<void(const FunctionReturnInfo&)>;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_RETURN_INFO_H_
