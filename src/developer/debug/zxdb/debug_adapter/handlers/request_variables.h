// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_H_
#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

void OnRequestVariables(
    DebugAdapterContext* ctx, const dap::VariablesRequest& req,
    const std::function<void(dap::ResponseOrError<dap::VariablesResponse>)>& callback);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_H_
