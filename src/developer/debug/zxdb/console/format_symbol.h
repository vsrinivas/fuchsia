// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_

#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class ProcessSymbols;
class Symbol;
class SymbolContext;

// Dumps the symbol information and returns a formatted buffer. The ProcessSymbols can be null but
// this means all offsets will be printed as relative, and no forward-declared types can be
// resolved (some information might be missing).
OutputBuffer FormatSymbol(const ProcessSymbols* process_symbols, const Symbol* symbol);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_
