// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_attach.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace dap {

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(AttachRequestZxdb, AttachRequest, "attach",
                                  DAP_FIELD(process, "process"))

}  // namespace dap

namespace zxdb {

dap::ResponseOrError<dap::AttachResponse> OnRequestAttach(DebugAdapterContext* context,
                                                          const dap::AttachRequestZxdb& req) {
  dap::AttachResponse response;
  Filter* filter = context->session()->system().CreateNewFilter();
  filter->SetPattern(req.process);
  return response;
}

}  // namespace zxdb
