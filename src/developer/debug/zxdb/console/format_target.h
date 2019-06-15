// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_TARGET_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_TARGET_H_

#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class ConsoleContext;

OutputBuffer FormatTarget(ConsoleContext* context, const Target* target);
OutputBuffer FormatTargetList(ConsoleContext* context, int indent = 0);

const char* TargetStateToString(Target::State state);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_TARGET_H_
