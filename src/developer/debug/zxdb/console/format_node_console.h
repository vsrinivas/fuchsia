// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NODE_CONSOLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NODE_CONSOLE_H_

#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/format_expr_value_options.h"

namespace zxdb {

struct FormatExprValueOptions;
class FormatNode;

// Console-output-specific options format formatting.
struct ConsoleFormatNodeOptions : public FormatExprValueOptions {};

// Formats the given FormatNode for the console.
//
// This assumes the node has been evaluated and described as desired by the caller so the result
// can be synchronously formatted and returned.
OutputBuffer FormatNodeForConsole(const FormatNode& node, const ConsoleFormatNodeOptions& options);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NODE_CONSOLE_H_
