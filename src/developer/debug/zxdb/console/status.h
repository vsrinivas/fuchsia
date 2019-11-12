// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STATUS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STATUS_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class ConsoleContext;
class Session;
class System;

// These functions return diagnostic and help information on the given
// category of stuff.
OutputBuffer GetConnectionStatus(const Session* session);
OutputBuffer GetJobStatus(ConsoleContext* context);
OutputBuffer GetProcessStatus(ConsoleContext* context);
OutputBuffer GetLimboStatus(const std::vector<debug_ipc::ProcessRecord>& limbo);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STATUS_H_
