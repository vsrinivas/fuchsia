// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

// Returns the hierarchy of parent scopes for the given symbol. This will
// be structs, classes, and namespaces that appear in the language scope
// qualifiers (e.g. foo::Bar::Baz). It will not include things that don't
// appear in the language like functions, lexical blocks, and compilation
// units.
//
// The return value will not include the input symbol itself, so if the input
// is the class definition for the C++ "string", this will return a vector of a
// single item referencing the "std" namespace.
std::vector<fxl::RefPtr<const Symbol>> GetSymbolScope(const Symbol* symbol);

// Convers a list of scope qualifiers (as returned by GetSymbolScope) to a
// string prefix. For C++ if the scope is nonempty, this will end with a
// "::" so that a symbol or type name can be appended to get a fully-qualified
// name. It will return the empty string if there is no qualifying scope.
std::string SymbolScopeToPrefixString(
    const std::vector<fxl::RefPtr<const Symbol>>& scope);

// This helper function gets the scope for the symbol (GetSymboLScope() above),
// and converts it to a string (SymbolScopeToPrefixString() above).
std::string GetSymbolScopePrefix(const Symbol* symbol);

// For types this will expand to the full type name, including "const", "*",
// and "&". For data and functions this will return the name including any
// namespaces, classes, and structs.
std::string GetFullyQualifiedSymbolName(const Symbol* symbol);

}  // namespace zxdb
