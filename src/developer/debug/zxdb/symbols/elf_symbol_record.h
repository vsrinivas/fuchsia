// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_RECORD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_RECORD_H_

#include <stdint.h>

#include <string>

namespace zxdb {

// Represents a symbol read from the ELF file. This holds the mangled and unmangled names for
// convenience for the index. Normal external users will use the ElfSymbol class which is
// constructed from this structure.
struct ElfSymbolRecord {
  ElfSymbolRecord() = default;

  // Automatically sets both the linkage name and unmangled name.
  explicit ElfSymbolRecord(uint64_t relative_address, const std::string& linkage_name);

  // Address relative to the beginning of the associated module of this symbol.
  uint64_t relative_address = 0;

  // The name from the ELF file. For C++ programs this will be the mangled name.
  std::string linkage_name;

  // Full unmangled name. Will be the same as the linkage_name if unmangling fails.
  //
  // Symbols for function names will include parens. This means it will NOT parse as an Identifier.
  // TODO(bug 41928) make Identifier support function parameters.
  std::string unmangled_name;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_RECORD_H_
