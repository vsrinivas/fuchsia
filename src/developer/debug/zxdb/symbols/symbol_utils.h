// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_

#include <vector>

#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// This helper function gets the scope for the symbol. This includes class and namespace names and
// will be glbally qualified, but does not include the name of the symbol itself. Use
// Symbol::GetFullName() for that.
Identifier GetSymbolScopePrefix(const Symbol* symbol);

// To make a regular tuple give it a name according to the types you use in parens, e.g. "(u32,
// Point)", to make a tuple struct, give it a word name like "Foo".
fxl::RefPtr<Collection> MakeRustTuple(const std::string& name,
                                      const std::vector<fxl::RefPtr<Type>>& members);

// Makes a type that can hold the raw string bytes of the given length. This always returns a
// C-style string array "char[length]". Rust's "&str" type is actually a structure with a pointer
// which we can't store as a literal in debugger client memory, and users expect an array
// "[char; 3]" to be printed as ['a', 'b', 'c'] instead of a string.
fxl::RefPtr<Type> MakeStringLiteralType(size_t length);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_UTILS_H_
