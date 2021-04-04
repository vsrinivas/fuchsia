// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_PAUSE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_PAUSE_H_
#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace zxdb {

void OnRequestPause(DebugAdapterContext* ctx, const dap::PauseRequest& request,
                    std::function<void(dap::ResponseOrError<dap::PauseResponse>)> callback);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_PAUSE_H_
