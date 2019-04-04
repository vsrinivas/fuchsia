// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/find_name.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/identifier.h"
#include "src/developer/debug/zxdb/expr/index_walker.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index_node.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/type_utils.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

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
FoundName GetNameFromDieList(
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
  return FoundName();
}

// Dispatcher for doing a find operation on each module in a process. This
// will attempt to prioritize the current module if the symbol context is
// non-null and will fall back to other modules if there's not a match in the
// current one.
//
// If the symbol context is null, the modules will be iterated in arbitrary
// order.
FoundName FindPerModule(
    const ProcessSymbols* process_symbols,
    const SymbolContext* optional_symbol_context,
    std::function<FoundName(const ModuleSymbols*)> per_module) {
  std::vector<const LoadedModuleSymbols*> modules =
      process_symbols->GetLoadedModuleSymbols();
  if (modules.empty())
    return FoundName();

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
      if (FoundName found = per_module(current_module->module_symbols()))
        return found;
    }
  }

  // Search all non-current modules.
  for (const LoadedModuleSymbols* loaded_mod : modules) {
    if (loaded_mod != current_module) {
      if (FoundName found = per_module(loaded_mod->module_symbols()))
        return found;
    }
  }
  return FoundName();
}

// Searches one particular index node for the given identifier.
FoundName FindPerIndexNode(const ModuleSymbols* module_symbols,
                           const IndexWalker& walker,
                           const Identifier& identifier) {
  if (identifier.components().empty())
    return FoundName();

  IndexWalker query_walker(walker);
  if (query_walker.WalkInto(identifier)) {
    // Found an exact match, see if it's actually something we can return.
    const ModuleSymbolIndexNode* match = query_walker.current();
    if (FoundName found = GetNameFromDieList(module_symbols, match->dies()))
      return found;
  }

  // We also want to know if there are any templates with that name, which will
  // look like "foo::bar<...". In that case, do a prefix search with an
  // appended "<" and see if there are any results.
  if (identifier.components().back().has_template()) {
    // When the input already has an explicit template (e.g. "vector<int>"),
    // there's no sense in doing the prefix search.
    return FoundName();
  }

  // Walk into all but the last node of the identifier (the last one is
  // potentially the template).
  IndexWalker template_walker(walker);
  if (!template_walker.WalkInto(identifier.GetScope()))
    return FoundName();

  // Now search that node for anything that could be a template.
  std::string last_comp = identifier.components().back().GetName(false, false);
  last_comp.push_back('<');
  auto [cur, end] = template_walker.current()->FindPrefix(last_comp);
  if (cur != end) {
    // We could check every possible match with this prefix and see if there's
    // one with a type name. But that seems unnecessary. Instead, assume that
    // anything with a name containing a "<" is a template type name.
    return FoundName(FoundName::kTemplate);
  }

  return FoundName();
}

}  // namespace

FoundName FindName(const ProcessSymbols* optional_process_symbols,
                   const CodeBlock* block,
                   const SymbolContext* optional_block_symbol_context,
                   const Identifier& identifier) {
  if (block && !identifier.InGlobalNamespace()) {
    // Search for local variables and function parameters.
    if (FoundName found = FindLocalVariable(block, identifier))
      return found;

    // Search the "this" object.
    if (FoundName found =
            FindMemberOnThis(optional_process_symbols, block,
                             optional_block_symbol_context, identifier))
      return found;
  }

  // Fall back to searching global vars.
  if (optional_process_symbols) {
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

    return FindGlobalName(optional_process_symbols, current_scope,
                          optional_block_symbol_context, identifier);
  }
  return FoundName();
}

FoundName FindLocalVariable(const CodeBlock* block,
                            const Identifier& identifier) {
  // TODO(DX-1214) lookup type names defined locally in this function.

  // Local variables can only be simple names.
  const std::string* name = identifier.GetSingleComponentName();
  if (!name)
    return FoundName();

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
  return FoundName();
}

FoundName FindMember(const ProcessSymbols* optional_process_symbols,
                     const SymbolContext* optional_block_symbol_context,
                     const Collection* object, const Identifier& identifier,
                     const Variable* optional_object_ptr) {
  // This code will check the object and all base classes.
  FoundName result;
  VisitClassHierarchy(
      object,
      [optional_process_symbols, optional_block_symbol_context, identifier,
       optional_object_ptr, &result](const Collection* cur_collection,
                                     uint64_t cur_offset) -> VisitResult {
        // Called for each collection in the hierarchy.

        // Data lookup.
        if (const std::string* ident_name =
                identifier.GetSingleComponentName()) {
          // TODO(brettw) allow "BaseClass::foo" syntax for specifically naming
          // a member of a base class. Watch out: the base class could be
          // qualified (or not) in various ways: ns::BaseClass::foo,
          // BaseClass::foo, etc.
          for (const auto& lazy : cur_collection->data_members()) {
            const DataMember* data = lazy.Get()->AsDataMember();
            if (data && data->GetAssignedName() == *ident_name) {
              result = FoundName(optional_object_ptr, data,
                                 cur_offset + data->member_location());
              return VisitResult::kDone;
            }
          }
        }

        // Type lookup if the caller gave us symbols.
        if (optional_process_symbols) {
          auto [err, to_look_up] =
              Identifier::FromString(cur_collection->GetFullName());
          if (!err.has_error()) {
            to_look_up.Append(identifier);
            if (FoundName found_type = FindExactNameInIndex(
                    optional_process_symbols, optional_block_symbol_context,
                    to_look_up)) {
              result = found_type;
              return VisitResult::kDone;
            }
          }
        }

        return VisitResult::kNotFound;  // Continue search.
      });
  return result;
}

FoundName FindMemberOnThis(const ProcessSymbols* optional_process_symbols,
                           const CodeBlock* block,
                           const SymbolContext* optional_block_symbol_context,
                           const Identifier& identifier) {
  const Function* function = block->GetContainingFunction();
  if (!function)
    return FoundName();
  const Variable* this_var = function->GetObjectPointerVariable();
  if (!this_var)
    return FoundName();  // No "this" pointer.

  // Pointed-to type for "this".
  const Collection* collection = nullptr;
  if (GetPointedToCollection(this_var->type().Get()->AsType(), &collection)
          .has_error())
    return FoundName();  // Symbols likely corrupt.

  if (FoundName member =
          FindMember(optional_process_symbols, optional_block_symbol_context,
                     collection, identifier, this_var))
    return member;
  return FoundName();
}

FoundName FindGlobalName(const ProcessSymbols* process_symbols,
                         const Identifier& current_scope,
                         const SymbolContext* symbol_context,
                         const Identifier& identifier) {
  return FindPerModule(process_symbols, symbol_context,
                       [&current_scope, &identifier](const ModuleSymbols* ms) {
                         return FindGlobalNameInModule(ms, current_scope,
                                                       identifier);
                       });
}

FoundName FindGlobalNameInModule(const ModuleSymbols* module_symbols,
                                 const Identifier& current_scope,
                                 const Identifier& identifier) {
  IndexWalker walker(&module_symbols->GetIndex());
  if (!identifier.InGlobalNamespace()) {
    // Unless the input identifier is fully qualified, start the search in the
    // current context.
    walker.WalkIntoClosest(current_scope);
  }

  // Search from the current namespace going up.
  do {
    if (FoundName found = FindPerIndexNode(module_symbols, walker, identifier))
      return found;
    // No variable match, move up one level of scope and try again.
  } while (walker.WalkUp());

  return FoundName();
}

FoundName FindExactNameInIndex(const ProcessSymbols* process_symbols,
                               const SymbolContext* symbol_context,
                               const Identifier& identifier) {
  return FindPerModule(process_symbols, symbol_context,
                       [&identifier](const ModuleSymbols* ms) {
                         return FindExactNameInModuleIndex(ms, identifier);
                       });
}

FoundName FindExactNameInModuleIndex(const ModuleSymbols* module_symbols,
                                     const Identifier& identifier) {
  return FindPerIndexNode(module_symbols,
                          IndexWalker(&module_symbols->GetIndex()), identifier);
}

}  // namespace zxdb
