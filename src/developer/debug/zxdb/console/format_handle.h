// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_HANDLE_H_

#include <vector>

#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace debug_ipc {
struct InfoHandle;
}

namespace zxdb {

// Formats a table of the handles with minimal information. The order of the table will be the
// same as the input vector. The hex flag prints values in hexadecimal. Otherwise decimal will be
// used.
OutputBuffer FormatHandles(const std::vector<debug_ipc::InfoHandle>& handles, bool hex);

// Formats a detailed summary of a single handle's information. The hex flag prints values in
// hexadecimal. Otherwise decimal will be used.
OutputBuffer FormatHandle(const debug_ipc::InfoHandle& handle, bool hex);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_HANDLE_H_
