// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_EVALUATE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_EVALUATE_H_

#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

void OnRequestEvaluate(
    DebugAdapterContext* ctx, const dap::EvaluateRequest& req,
    const std::function<void(dap::ResponseOrError<dap::EvaluateResponse>)>& callback);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_EVALUATE_H_
