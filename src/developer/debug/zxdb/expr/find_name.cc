// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/find_name.h"

#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"
#include "garnet/bin/zxdb/symbols/module_symbols.h"
#include "garnet/bin/zxdb/symbols/namespace.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"
#include "garnet/bin/zxdb/symbols/type_utils.h"
#include "garnet/bin/zxdb/symbols/visit_scopes.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/identifier.h"
#include "src/developer/debug/zxdb/expr/index_walker.h"

namespace zxdb {

namespace {

// Searches the given vector of values for one with the given name. If found,
// returns it, otherwise returns null.
const Variable* SearchVariableVector(const std::vector<LazySymbol>& vect,
                                     const std::string& search_for) {
  for (const auto& cur : vect) {
    const Variable* var = cur.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    if (search_for == var->GetAssignedName())
      return var;
  }
  return nullptr;
}

// Searches the list for a reference to a name and returns the first variable,
// namespace, or type it finds.
std::optional<FoundName> GetNameFromDieList(
    const ModuleSymbols* module_symbols,
    const std::vector<ModuleSymbolIndexNode::DieRef>& dies) {
  for (const ModuleSymbolIndexNode::DieRef& cur : dies) {
    LazySymbol lazy_symbol = module_symbols->IndexDieRefToSymbol(cur);
    if (!lazy_symbol)
      continue;
    const Symbol* symbol = lazy_symbol.Get();

    if (const Variable* var = symbol->AsVariable())
      return FoundName(var);
    if (const Namespace* ns = symbol->AsNamespace())
      return FoundName(FoundName::kNamespace);
    if (const Type* type = symbol->AsType())
      return FoundName(fxl::RefPtr<Type>(const_cast<Type*>(type)));
  }
  return std::nullopt;
}

// Dispatcher for doing a find operation on each module in a process. This
// will attempt to prioritize the current module if the symbol context is
// non-null and will fall back to other modules if there's not a match in the
// current one.
//
// If the symbol context is null, the modules will be iterated in arbitrary
// order.
std::optional<FoundName> FindPerModule(
    const ProcessSymbols* process_symbols,
    const SymbolContext* optional_symbol_context,
    std::function<std::optional<FoundName>(const ModuleSymbols*)> per_module) {
  std::vector<const LoadedModuleSymbols*> modules =
      process_symbols->GetLoadedModuleSymbols();
  if (modules.empty())
    return std::nullopt;

  // When we're given a block to start searching from, always search
  // that module for symbol matches first. If there are duplicates in other
  // modules, one normally wants the current one.
  const LoadedModuleSymbols* current_module = nullptr;
  if (optional_symbol_context) {
    // Find the module that corresponds to the symbol context.
    uint64_t module_load_address =
        optional_symbol_context->RelativeToAbsolute(0);
    for (const LoadedModuleSymbols* mod : modules) {
      if (mod->load_address() == module_load_address) {
        current_module = mod;
        break;
      }
    }

    if (current_module) {
      // Search the current module.
      if (auto found = per_module(current_module->module_symbols()))
        return found;
    }
  }

  // Search all non-current modules.
  for (const LoadedModuleSymbols* loaded_mod : modules) {
    if (loaded_mod != current_module) {
      if (auto found = per_module(loaded_mod->module_symbols()))
        return found;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<FoundName> FindName(const ProcessSymbols* process_symbols,
                                  const CodeBlock* block,
                                  const SymbolContext* block_symbol_context,
                                  const Identifier& identifier) {
  if (block && !identifier.InGlobalNamespace()) {
    // Search for local variables and function parameters.
    if (auto found = FindLocalVariable(block, identifier))
      return found;

    // Search the "this" object.
    if (auto found = FindMemberOnThis(block, identifier))
      return found;
  }

  // Fall back to searching global vars.
  if (process_symbols) {
    // Get the scope for the current function. This may fail in which case
    // we'll be left with an empty current scope. This is non-fatal: it just
    // means we won't implicitly search the current namespace and will search
    // only the global one.
    Identifier current_scope;
    if (const Function* function = block->GetContainingFunction()) {
      auto [err, func_name] = Identifier::FromString(function->GetFullName());
      if (!err.has_error())
        current_scope = func_name.GetScope();
    }

    return FindGlobalName(process_symbols, current_scope, block_symbol_context,
                          identifier);
  }
  return std::nullopt;
}

std::optional<FoundName> FindLocalVariable(const CodeBlock* block,
                                           const Identifier& identifier) {
  // TODO(DX-1214) lookup type names defined locally in this function.

  // Local variables can only be simple names.
  const std::string* name = identifier.GetSingleComponentName();
  if (!name)
    return std::nullopt;

  // Search backwards in the nested lexical scopes searching for the first
  // variable or function parameter with the given name.
  const CodeBlock* cur_block = block;
  while (cur_block) {
    // Check for variables in this block.
    if (auto* var = SearchVariableVector(cur_block->variables(), *name))
      return FoundName(var);

    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (auto* var = SearchVariableVector(function->parameters(), *name))
        return FoundName(var);
      break;  // Don't recurse into higher levels of nesting than a function.
    }
    if (!cur_block->parent())
      break;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }
  return std::nullopt;
}

std::optional<FoundName> FindMember(const Collection* object,
                                    const Identifier& identifier,
                                    const Variable* optional_object_ptr) {
  // TODO(brettw) allow "BaseClass::foo" syntax for specifically naming a
  // member of a base class. Watch out: the base class could be qualified (or
  // not) in various ways: ns::BaseClass::foo, BaseClass::foo, etc.
  const std::string* ident_name = identifier.GetSingleComponentName();
  if (!ident_name)
    return std::nullopt;

  // This code will check the object and all base classes.
  std::optional<FoundName> result;
  VisitClassHierarchy(
      object,
      [ident_name, optional_object_ptr, &result](
          const Collection* cur_collection,
          uint32_t cur_offset) -> VisitResult {
        // Called for each collection in the hierarchy.

        // Data lookup.
        for (const auto& lazy : cur_collection->data_members()) {
          const DataMember* data = lazy.Get()->AsDataMember();
          if (data && data->GetAssignedName() == *ident_name) {
            result.emplace(optional_object_ptr, data,
                           cur_offset + data->member_location());
            return VisitResult::kDone;
          }
        }

        // Type lookup.
        /*
        TODO(brettw) hook up type names here. This requires piping through
        the ProcessSymbols so the names can be looked up in the index. It will
        look something like this:

        auto [err, ident] Identifier::FromString(cur_collection->GetFullName());
        if (!err.has_error) {
          ident.Append(identifier);
          if (auto found_type = FindExactNameInIndex(process_symbols, ident))
            return found_type;
        }
        */

        return VisitResult::kNotFound;  // Continue search.
      });
  return result;
}

std::optional<FoundName> FindMemberOnThis(const CodeBlock* block,
                                          const Identifier& identifier) {
  const Function* function = block->GetContainingFunction();
  if (!function)
    return std::nullopt;
  const Variable* this_var = function->GetObjectPointerVariable();
  if (!this_var)
    return std::nullopt;  // No "this" pointer.

  // Pointed-to type for "this".
  const Collection* collection = nullptr;
  if (GetPointedToCollection(this_var->type().Get()->AsType(), &collection)
          .has_error())
    return std::nullopt;  // Symbols likely corrupt.

  if (auto member = FindMember(collection, identifier, this_var))
    return std::move(*member);
  return std::nullopt;
}

std::optional<FoundName> FindGlobalName(const ProcessSymbols* process_symbols,
                                        const Identifier& current_scope,
                                        const SymbolContext* symbol_context,
                                        const Identifier& identifier) {
  return FindPerModule(process_symbols, symbol_context,
                       [&current_scope, &identifier](const ModuleSymbols* ms) {
                         return FindGlobalNameInModule(ms, current_scope,
                                                       identifier);
                       });
}

std::optional<FoundName> FindGlobalNameInModule(
    const ModuleSymbols* module_symbols, const Identifier& current_scope,
    const Identifier& identifier) {
  IndexWalker walker(&module_symbols->GetIndex());
  if (!identifier.InGlobalNamespace()) {
    // Unless the input identifier is fully qualified, start the search in the
    // current context.
    walker.WalkIntoClosest(current_scope);
  }

  // Search from the current namespace going up.
  do {
    IndexWalker query_walker(walker);
    if (query_walker.WalkInto(identifier)) {
      // Found a match, see if it's actually something we can return.
      const ModuleSymbolIndexNode* match = query_walker.current();
      if (auto found = GetNameFromDieList(module_symbols, match->dies()))
        return found;
    }
    // TODO(brettw) handle templates.

    // No variable match, move up one level of scope and try again.
  } while (walker.WalkUp());

  return std::nullopt;
}

std::optional<FoundName> FindExactNameInIndex(
    const ProcessSymbols* process_symbols, const SymbolContext* symbol_context,
    const Identifier& identifier) {
  return FindPerModule(process_symbols, symbol_context,
                       [&identifier](const ModuleSymbols* ms) {
                         return FindExactNameInModuleIndex(ms, identifier);
                       });
}

std::optional<FoundName> FindExactNameInModuleIndex(
    const ModuleSymbols* module_symbols, const Identifier& identifier) {
  std::vector<std::string> comps = identifier.GetAsIndexComponents();
  if (comps.empty())
    return std::nullopt;

  // Given input "foo::bar" first look up whether there's a variable,
  // namespace, or type name with that exact match.
  const std::vector<ModuleSymbolIndexNode::DieRef>& refs =
      module_symbols->GetIndex().FindExact(comps);
  if (auto found = GetNameFromDieList(module_symbols, refs))
    return found;

  // We also want to know if there are any templates with that name, which will
  // look like "foo::bar<...". In that case, do a prefix search with an
  // appended "<" and see if there are any results.
  if (identifier.components().back().has_template()) {
    // When the input already has an explicit template (e.g. "vector<int>"),
    // there's no sense in doing the prefix search.
    return std::nullopt;
  }

  comps.back().push_back('<');
  auto [cur, end] = module_symbols->GetIndex().FindPrefix(comps);
  if (cur != end) {
    // We could check every possible match with this prefix and see if there's
    // one with a type name. But that seems unnecessary. Instead, assume that
    // anything with a name containing a "<" is a template type name.
    return FoundName(FoundName::kTemplate);
  }

  return std::nullopt;
}

}  // namespace zxdb
