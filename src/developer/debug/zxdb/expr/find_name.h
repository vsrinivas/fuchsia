// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
class TargetSymbols;
class Variable;

// FindName can search for different levels of things depending on how much
// context it's given. This class encapsulates all of these variants.
struct FindNameContext {
  // No symbol context. This can be useful when searching for names on
  // structures where there is no environmental state needed.
  FindNameContext() = default;

  // Search everything given a live context. The current module is extracted
  // from the given symbol context if possible.
  //
  // Note that this tolerates a null ProcessSymbols which sets no symbol
  // paths. This is useful for some tests.
  FindNameContext(const ProcessSymbols* ps, const SymbolContext& symbol_context,
                  const CodeBlock* cb = nullptr);

  // Searches a target's symbols. This is used to search for symbols in a
  // non-running program.
  explicit FindNameContext(const TargetSymbols* ts);

  // Together TargetSymbols and ModuleSymbols control what is searched. They
  // are both optional, producing this behavior:
  //
  // - Both TargetSymbols and ModuleSymbols: All modules will be searched with
  //   the given one searched first. This is to give preference to the current
  //   module in the case of multiple matches.
  //
  // - TargetSymbols but not ModuleSymbols: All modules will be searched in an
  //   arbitrary order.
  //
  // - ModuleSymbols but not TargetSymbols: Only the given module will be
  //   searched for symbols.
  //
  // - Neither TargetSymbols nor ModuleSymbols: No symbol lookups are done.
  const TargetSymbols* target_symbols = nullptr;
  const ModuleSymbols* module_symbols = nullptr;

  // If given, local variables, local types, and |this| will be searched.
  // Otherwise, only global symbols will be searched.
  const CodeBlock* block = nullptr;
};

// Main variable and type name finding function. Searches the local, "this",
// and global scopes for a variable with the given identifier name.
//
// The block identifies the scope to search from (the class and namespace of
// the function will be searched). The block can be a null pointer in which
// case only the global scope will be searched. If a block is given, the
// block_symbol_context must also be given which identifies the module that
// the block is from. This also allows prioritization of symbols from the
// current process.
//
// The optional_process_symbols is used to search for global variables and
// all type names (including local and |this| member types), it can be null in
// which case only local variables will be searched.
FoundName FindName(const FindNameContext& context,
                   const Identifier& identifier);

// Type-specific finding -------------------------------------------------------

// Searches the code block for local variables. This includes all nested code
// blocks and function parameters, but does not go into the "this" class or any
// non-function scopes like the current or global namespace (that's what the
// later functions do).
FoundName FindLocalVariable(const CodeBlock* block,
                            const Identifier& identifier);

// Searches for the named variable or type on the given collection. This is the
// lower-level function and assumes a valid object. The result can be either a
// kType or a kMemberVariable.
//
// If the ProcessSymbols is non-null, this function will also search for
// type names defined in the collection. Otherwise, only data members will be
// searched.
//
// The optional symbol context is the symbol context for the current code. it
// will be used to prioritize symbol searching to the current module if given.
//
// If the result is a member variable, the optional_object_ptr will be used to
// construct the FoundName object. It can be null if the caller does not have
// a variable for the object it's looking up (just doing a type query).
FoundName FindMember(const FindNameContext& context, const Collection* object,
                     const Identifier& identifier,
                     const Variable* optional_object_ptr);

// Attempts the resolve the given named member variable or type on the "this"
// pointer associated with the given code block. Fails if the function has no
// "this" pointer or the type name / data member isn't found.
//
// If the ProcessSymbols is non-null, this function will also search for
// type names defined in the collection. Otherwise, only data members will be
// searched.
//
// The optional symbol context is the symbol context for the current code. it
// will be used to prioritize symbol searching to the current module if given.
FoundName FindMemberOnThis(const FindNameContext& context,
                           const Identifier& identifier);

// Attempts to resolve the named variable or type in the global namespace and
// any other namespaces that the given block is in.
//
// The symbol_context is used to prioritize the current module. It can be null
// to search in a non-guaranteed order.
FoundName FindGlobalName(const FindNameContext& context,
                         const Identifier& current_scope,
                         const Identifier& identifier);

// Searches a specific index and current namespace for a global variable or
// type of the given name. The current_scope would be the current namespace +
// class from where to start the search.
FoundName FindGlobalNameInModule(const ModuleSymbols* module_symbols,
                                 const Identifier& current_scope,
                                 const Identifier& identifier);

// Searches the index for the exact identifier name. Unlike
// FindGlobalName[InModule] it doesn't take into account the current scope: the
// input name must be fully-qualified.
//
// This will resolve namespaces, types, and templates (type names before the
// '<'). It is used as helper when traversing scopes to look for types that
// might be in the current scope.
//
// The symbol_context is used to prioritize the current module. It can be null
// to search in a non-guaranteed order.
FoundName FindExactNameInIndex(const FindNameContext& context,
                               const Identifier& identifier);

// Per-module version of FindExactNameInIndex().
FoundName FindExactNameInModuleIndex(const ModuleSymbols* module_symbols,
                                     const Identifier& identifier);

}  // namespace zxdb
