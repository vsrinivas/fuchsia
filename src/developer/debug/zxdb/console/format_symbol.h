// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class Command;
class ProcessSymbols;
class Symbol;
class SymbolContext;

struct FormatSymbolOptions {
  // How DWARF expressions are printed.
  enum class DwarfExpr {
    kBytes,   // Hex bytes.
    kOps,     // Basic stringification of DWARF operationrs.
    kPretty,  // Pretty print with register names, etc.
  };

  // For decoding architecture-specific symbols. "Unknown" disables.
  debug_ipc::Arch arch = debug_ipc::Arch::kUnknown;

  DwarfExpr dwarf_expr = DwarfExpr::kPretty;
};

// Dumps the symbol information and returns a formatted buffer. The ProcessSymbols can be null but
// this means all offsets will be printed as relative, and no forward-declared types can be
// resolved (some information might be missing).
OutputBuffer FormatSymbol(const ProcessSymbols* process_symbols, const Symbol* symbol,
                          const FormatSymbolOptions& opts);

// Reads the FormatSymbolOptions for the given command. The expr_switch is the switch index that
// specifies how DWARF expressions should be printed.
ErrOr<FormatSymbolOptions> GetFormatSymbolOptionsFromCommand(const Command& cmd, int expr_switch);

#define DWARF_EXPR_COMMAND_SWTICH_HELP                                             \
  "  --dwarf-expr=(bytes | ops | pretty)\n"                                        \
  "      Controls how DWARF expressions are presented (defaults to \"pretty\"):\n" \
  "\n"                                                                             \
  "       • bytes:  Print raw hex bytes.\n"                                      \
  "       • ops:    Print DWARF constants.\n"                                    \
  "       • pretty: Decodes variable names and addresses and simplifies output.\n"

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_SYMBOL_H_
