// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_

#include <vector>

#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// This helper function gets the scope for the symbol. This includes class and
// namespace names, but does not include the name of the symbol itself. Use
// Symbol::GetFullName() for that.
Identifier GetSymbolScopePrefix(const Symbol* symbol);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_
