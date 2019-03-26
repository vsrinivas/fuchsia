// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <string>

namespace zxdb {

class CodeBlock;
class Collection;
class Identifier;
class FoundMember;
class FoundName;
class Location;
class ModuleSymbols;
class ProcessSymbols;
class SymbolContext;

// Main variable finding function. Searches the local, "this", and global
// scopes for a variable with the given identifier name.
//
// The block identifies the scope to search from (the class and namespace of
// the function will be searched). The block can be a null pointer in which
// case only the global scope will be searched. If a block is given, the
// block_symbol_context must also be given which identifies the module that
// the block is from. This also allows prioritization of symbols from the
// current process.
//
// The process_symbols is used to search for global variables, it can be null
// in which case only local variables will be searched.
std::optional<FoundName> FindName(const ProcessSymbols* process_symbols,
                                  const CodeBlock* block,
                                  const SymbolContext* block_symbol_context,
                                  const Identifier& identifier);

// Type-specific finding -------------------------------------------------------

// Searches the give code block for local variables. This includes all nested
// code blocks and function parameters, but does not go into the "this" class
// or any non-function scopes like the current or global namespace (that's
// what the later functions do).
std::optional<FoundName> FindLocalVariable(const CodeBlock* block,
                                           const Identifier& identifier);

// Searches for the given variable name on the given collection. This is the
// lower-level function and assumes a valid object.
std::optional<FoundMember> FindMember(const Collection* object,
                                      const Identifier& identifier);

// Attempts the resolve the given named member variable on the "this" pointer
// associated with the given code block. Fails if the function has no "this"
// pointer or the member isn't found.
std::optional<FoundName> FindMemberOnThis(const CodeBlock* block,
                                          const Identifier& identifier);

// Attempts to resolve the named variable in the global namespace and any
// other namespaces that the given block is in. The symbol_context is used
// to prioritize the current module. It can be null to search in a
// non-guaranteed order.
std::optional<FoundName> FindGlobalName(const ProcessSymbols* process_symbols,
                                        const Identifier& current_scope,
                                        const SymbolContext* symbol_context,
                                        const Identifier& identifier);

// Searches a specific index and current namespace for a global variable of
// the given name. The current_scope would be the current namespace + class
// from where to start the search.
std::optional<FoundName> FindGlobalNameInModule(
    const ModuleSymbols* module_symbols, const Identifier& current_scope,
    const Identifier& identifier);

}  // namespace zxdb
