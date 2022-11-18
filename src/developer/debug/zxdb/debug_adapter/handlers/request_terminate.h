// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_TERMINATE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_TERMINATE_H_

#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

void OnRequestTerminate(
    DebugAdapterContext* ctx, const dap::TerminateRequest& req,
    const std::function<void(dap::ResponseOrError<dap::TerminateResponse>)>& callback);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_TERMINATE_H_
