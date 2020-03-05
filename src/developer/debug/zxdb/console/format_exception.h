// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_EXCEPTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_EXCEPTION_H_

#include <string>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class ConsoleContext;
class Thread;

// Formats the given exception along with some additional information.
OutputBuffer FormatException(const ConsoleContext* context, const Thread* thread,
                             const debug_ipc::ExceptionRecord& record);

// Converts the exception record to a single string describing the exception that occurred.
std::string ExceptionRecordToString(debug_ipc::Arch arch, const debug_ipc::ExceptionRecord& record);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_EXCEPTION_H_
