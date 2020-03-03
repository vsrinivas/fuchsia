// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STOP_INFO_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STOP_INFO_H_

#include "src/developer/debug/ipc/records.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;

// Information on a thread stop notification.
struct StopInfo {
  debug_ipc::ExceptionType exception_type = debug_ipc::ExceptionType::kNone;

  debug_ipc::ExceptionRecord exception_record;

  // Breakpoints at this address. There can be more than one breakpoint at the same address.
  //
  // These are weak poiners because there can be multiple observers and certain observers might
  // remove breakpoints in response to the notification, leaving it null for later observers.
  std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STOP_INFO_H_
