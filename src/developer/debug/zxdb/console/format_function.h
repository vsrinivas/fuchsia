// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FUNCTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FUNCTION_H_

#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class Function;
class TargetSymbols;

// Formats the function name with syntax highlighting.
//
// It will apply some simple rewrite rules to clean up some symbols.
//
// If optional_target_symbols is provided it can provide extra cleanup for
// some generated lambda names by using the shortest possible unique file name.
//
// If show_params is true, the types of the function parameters will be output.
// Otherwise the function name will end with "()" if there are no parameters,
// or "(...)" if there are some. The goal is to be as short as possible without
// being misleading (showing "()" when there are parameters may be misleading,
// and no parens at all don't look like a function).
OutputBuffer FormatFunctionName(const Function* function, bool show_params);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FUNCTION_H_
