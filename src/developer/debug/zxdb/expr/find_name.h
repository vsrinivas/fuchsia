// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FIND_NAME_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FIND_NAME_H_

#include <limits>
#include <optional>
#include <string>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

namespace zxdb {

class CodeBlock;
class Collection;
class DataMember;
class FoundMember;
class FoundName;
class Location;
class ModuleSymbols;
class ProcessSymbols;
class SymbolContext;
class TargetSymbols;
class Variable;

// ==========
//  OVERVIEW
// ==========
//
// FindName is the general lookup for names of things. It understands the hierarchy of naming of the
// current context and follows C++ rules for resolving names. It can also do prefix searches for
// autocompletion.
//
// It provides a superset of the symbol lookup functionality of the symbol system's
// ResolveInputLocation() functions. The symbol system provides only exact matching.
//
// ALMOST ALL CALLERS SHOULD USE EvalContext::FindName() INSTEAD. This automatically hooks up the
// correct symbol information (which can be a bit complicated) and also allows tests to inject
// names of things without having to mock the entire symbol system.

// FindName can search for different levels of things depending on how much context it's given. This
// class encapsulates all of these variants.
struct FindNameContext {
  // No symbol context. This can be useful when searching for names on structures where there is no
  // environmental state needed.
  explicit FindNameContext(std::optional<ExprLanguage> lang = std::nullopt) : language(lang) {}

  // Search everything given a live context. The current module is extracted from the given symbol
  // context if possible. This can be SymbolContext::ForRelativeAddresses() to skip this.
  //
  // Note that this tolerates a null ProcessSymbols which sets no symbol paths. This is useful for
  // some tests.
  FindNameContext(const ProcessSymbols* ps,
                  const SymbolContext& symbol_context = SymbolContext::ForRelativeAddresses(),
                  const CodeBlock* cb = nullptr, std::optional<ExprLanguage> lang = std::nullopt);

  // Searches a target's symbols. This is used to search for symbols in a non-running program.
  explicit FindNameContext(const TargetSymbols* ts,
                           std::optional<ExprLanguage> lang = std::nullopt);

  // Together TargetSymbols and ModuleSymbols control what is searched. They are both optional,
  // producing this behavior:
  //
  // - Both TargetSymbols and ModuleSymbols: All modules will be searched with the given one
  //   searched first. This is to give preference to the current module in the case of multiple
  //   matches.
  //
  // - TargetSymbols but not ModuleSymbols: All modules will be searched in an arbitrary order.
  //
  // - ModuleSymbols but not TargetSymbols: Only the given module will be searched for symbols.
  //
  // - Neither TargetSymbols nor ModuleSymbols: No symbol lookups are done.
  const TargetSymbols* target_symbols = nullptr;
  const ModuleSymbols* module_symbols = nullptr;

  // If given, local variables, local types, and |this| will be searched. Otherwise, only global
  // symbols will be searched.
  const CodeBlock* block = nullptr;

  // The language to search built-in types for. If set and there are no type matches, the name
  // will be matched against hardcoded built-in types for the corresponding language. If unset,
  // only types declared in the symbols will be matched.
  std::optional<ExprLanguage> language;
};

// By default this will find the first exact match of any kind.
struct FindNameOptions {
  // How to match the name.
  //
  // Note that prefix matching doesn't currently work for templates. Prefix matching is currently
  // used for autocomplete where the full type name is desired, not just the base template name. And
  // supporting this requires uniquifying names (since many template types could be the same
  // underlying template) that's annoying to implement.
  enum HowMatch { kPrefix, kExact };
  HowMatch how = kExact;

  enum SearchMode {
    // A lexical search is a normal search starting from the current scope and searching outward
    // from there. This is the normal search that programmers expect when typing names in a
    // language.
    kLexical,

    // An "all namespaces" search ignores the current scope and recursively searches all namespaces
    // for matches for a given name. This can be the desired behavior for things like finding
    // functions for breakpoints, but this search will never find local or class variables.
    //
    // Fully qualified identifiers ("::Foo") will not get implicit namespace searching, even when
    // requested. They will only match the toplevel.
    //
    // This mode is only valid for full index searches via FindName() and FindIndexedName().
    // The local searching variants like FindLocalVariable() and FindMember() do not support it.
    kAllNamespaces,
  };
  SearchMode search_mode = kLexical;

  // This constructor's argument indicates whether the caller wants to default to finding all or no
  // types (presumably in the "no types" case, the caller will set one or more to true afterward).
  enum InitialKinds : bool { kNoKinds = false, kAllKinds = true };
  explicit FindNameOptions(InitialKinds initial)
      : find_types(initial),
        find_type_defs(initial),
        find_functions(initial),
        find_vars(initial),
        find_templates(initial),
        find_namespaces(initial) {}

  // The types of named things that will be matched.
  bool find_types = true;
  bool find_type_defs = true;  // Subset of "types": definitions only, not forward declarations.
  bool find_functions = true;  // Global and member functions.
  bool find_vars = true;       // Local and "this" member vars.
  bool find_templates = true;  // Templatized types without <...>.
  bool find_namespaces = true;

  constexpr static size_t kAllResults = std::numeric_limits<size_t>::max();

  size_t max_results = 1;  // Use kAllResults to get everything.
};

// Main variable and type name finding function. Searches the local, "this", and global scopes for
// one or more things with a matching name. The first version finds the first exact match of any
// type, the second uses the options to customize what and how many results are returned.
//
// The variant that returns a single value ignores the max_results of the options and always returns
// the first thing.
FoundName FindName(const FindNameContext& context, const FindNameOptions& options,
                   const ParsedIdentifier& identifier);
void FindName(const FindNameContext& context, const FindNameOptions& options,
              const ParsedIdentifier& looking_for, std::vector<FoundName>* results);

// Type-specific finding ---------------------------------------------------------------------------

// Searches the code block for local variables. This includes all nested code blocks and function
// parameters, but does not go into the "this" class or any non-function scopes like the current or
// global namespace (that's what the later functions do).
//
// The "visit" variant calls the callback for every variable in order of priority for as long as the
// visitor reports "continue." The "find" variant does an exact string search, the "prefix" variant
// does a prefix search.
VisitResult VisitLocalVariables(const CodeBlock* block,
                                fit::function<VisitResult(const Variable*)> visitor);
void FindLocalVariable(const FindNameOptions& options, const CodeBlock* block,
                       const ParsedIdentifier& looking_for, std::vector<FoundName>* results);

// Searches for the named variable or type on the given collection. This is the lower-level function
// and assumes a valid object. The result can be either a kType or a kMemberVariable.
//
// If the ProcessSymbols is non-null, this function will also search for type names defined in the
// collection. Otherwise, only data members will be searched.
//
// The optional symbol context is the symbol context for the current code. it will be used to
// prioritize symbol searching to the current module if given.
//
// If the result is a member variable, the optional_object_ptr will be used to construct the
// FoundName object. It can be null if the caller does not have a variable for the object it's
// looking up (just doing a type query).
void FindMember(const FindNameContext& context, const FindNameOptions& options,
                const Collection* object, const ParsedIdentifier& looking_for,
                const Variable* optional_object_ptr, std::vector<FoundName>* result);

// Attempts the resolve the given named member variable or type on the "this" pointer associated
// with the given code block. Fails if the function has no "this" pointer or the type name / data
// member isn't found.
//
// If the ProcessSymbols is non-null, this function will also search for type names defined in the
// collection. Otherwise, only data members will be searched.
//
// The optional symbol context is the symbol context for the current code. it will be used to
// prioritize symbol searching to the current module if given.
void FindMemberOnThis(const FindNameContext& context, const FindNameOptions& options,
                      const ParsedIdentifier& looking_for, std::vector<FoundName>* result);

// Attempts to resolve the named |looking_for| in the index.
//
// The |current_scope| is the namespace to start looking in. If |search_containing| is true, parent
// scopes of the |current_scope| are also searched, otherwise only exact matches in that scope will
// be found.
VisitResult FindIndexedName(const FindNameContext& context, const FindNameOptions& options,
                            const ParsedIdentifier& current_scope,
                            const ParsedIdentifier& looking_for, bool search_containing,
                            std::vector<FoundName>* results);

// Searches a specific index and current namespace for a global variable or type of the given name.
// The current_scope would be the current namespace + class from where to start the search.
void FindIndexedNameInModule(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                             const ParsedIdentifier& current_scope,
                             const ParsedIdentifier& looking_for, bool search_containing,
                             std::vector<FoundName>* results);

// In many contexts (like function parameters and local variables) an identifier name can't have any
// :: or template parameters and can have only one component. If this identifier satisfies this
// requirement, a pointer to the single string is returned. If there is zero or more than one
// component or any template specs, returns null.
//
// The returned pointer will be invalidated if the Identifier is mutated.
const std::string* GetSingleComponentIdentifierName(const ParsedIdentifier& ident);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FIND_NAME_H_
