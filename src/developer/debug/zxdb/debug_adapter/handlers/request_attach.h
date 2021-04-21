// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_ATTACH_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_ATTACH_H_

#include <dap/typeof.h>

#include "src/developer/debug/zxdb/debug_adapter/context.h"

namespace dap {
class AttachRequestZxdb : public AttachRequest {
 public:
  dap::string process;
};
DAP_DECLARE_STRUCT_TYPEINFO(AttachRequestZxdb);
}  // namespace dap

namespace zxdb {
dap::ResponseOrError<dap::AttachResponse> OnRequestAttach(DebugAdapterContext* ctx,
                                                          const dap::AttachRequestZxdb& req);
}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_ATTACH_H_
