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
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
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

// Dispatcher for doing a find operation on each module in a context. See
// FindNameContext for how module searching is priorized.
FoundName FindPerModule(
    const FindNameContext& context,
    std::function<FoundName(const ModuleSymbols*)> per_module) {
  if (context.module_symbols) {
    // Search in the current module.
    if (FoundName found = per_module(context.module_symbols))
      return found;
  }

  // Search in all other modules as a fallback, if any.
  if (context.target_symbols) {
    for (const ModuleSymbols* m : context.target_symbols->GetModuleSymbols()) {
      if (m != context.module_symbols) {  // Don't re-search current one.
        if (FoundName found = per_module(m))
          return found;
      }
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

FindNameContext::FindNameContext(const ProcessSymbols* ps,
                                 const SymbolContext& symbol_context,
                                 const CodeBlock* b)
    : block(b) {
  if (ps) {
    target_symbols = ps->target_symbols();

    // Find the module that corresponds to the symbol context.
    uint64_t module_load_address = symbol_context.RelativeToAbsolute(0);
    for (const LoadedModuleSymbols* mod : ps->GetLoadedModuleSymbols()) {
      if (mod->load_address() == module_load_address) {
        module_symbols = mod->module_symbols();
        break;
      }
    }
  }
}

FindNameContext::FindNameContext(const TargetSymbols* ts)
    : target_symbols(ts) {}

FoundName FindName(const FindNameContext& context,
                   const Identifier& identifier) {
  if (context.block && !identifier.InGlobalNamespace()) {
    // Search for local variables and function parameters.
    if (FoundName found = FindLocalVariable(context.block, identifier))
      return found;

    // Search the "this" object.
    if (FoundName found = FindMemberOnThis(context, identifier))
      return found;
  }

  // Fall back to searching global vars.
  if (context.module_symbols || context.target_symbols) {
    // Get the scope for the current function. This may fail in which case
    // we'll be left with an empty current scope. This is non-fatal: it just
    // means we won't implicitly search the current namespace and will search
    // only the global one.
    Identifier current_scope;
    if (context.block) {
      if (const Function* function = context.block->GetContainingFunction()) {
        auto [err, func_name] = Identifier::FromString(function->GetFullName());
        if (!err.has_error())
          current_scope = func_name.GetScope();
      }
    }
    return FindGlobalName(context, current_scope, identifier);
  }
  return FoundName();
}

FoundName FindLocalVariable(const CodeBlock* block,
                            const Identifier& identifier) {
  // TODO(DX-1214) lookup type names defined locally in this function.
  FoundName result;

  // Local variables can only be simple names.
  const std::string* name = identifier.GetSingleComponentName();
  if (!name)
    return result;

  VisitLocalBlocks(block, [name, &result](const CodeBlock* cur_block) {
    // Local variables in this block.
    if (auto* var = SearchVariableVector(cur_block->variables(), *name)) {
      result = FoundName(var);
      return VisitResult::kDone;
    }

    // Function parameters.
    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (auto* var = SearchVariableVector(function->parameters(), *name)) {
        result = FoundName(var);
        return VisitResult::kDone;
      }
    }
    return VisitResult::kContinue;
  });

  return result;
}

FoundName FindMember(const FindNameContext& context, const Collection* object,
                     const Identifier& identifier,
                     const Variable* optional_object_ptr) {
  // This code will check the object and all base classes.
  FoundName result;
  VisitClassHierarchy(
      object,
      [&context, identifier, optional_object_ptr, &result](
          const Collection* cur_collection,
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

        // Type lookup (will be a nop-op if the context specifies no symbols).
        auto [err, to_look_up] =
            Identifier::FromString(cur_collection->GetFullName());
        if (!err.has_error()) {
          to_look_up.Append(identifier);
          if (FoundName found_type =
                  FindExactNameInIndex(context, to_look_up)) {
            result = found_type;
            return VisitResult::kDone;
          }
        }

        return VisitResult::kContinue;
      });
  return result;
}

FoundName FindMemberOnThis(const FindNameContext& context,
                           const Identifier& identifier) {
  if (!context.block)
    return FoundName();  // No current code.
  const Function* function = context.block->GetContainingFunction();
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

  if (FoundName member = FindMember(context, collection, identifier, this_var))
    return member;
  return FoundName();
}

FoundName FindGlobalName(const FindNameContext& context,
                         const Identifier& current_scope,
                         const Identifier& identifier) {
  return FindPerModule(
      context, [&current_scope, &identifier](const ModuleSymbols* ms) {
        return FindGlobalNameInModule(ms, current_scope, identifier);
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

FoundName FindExactNameInIndex(const FindNameContext& context,
                               const Identifier& identifier) {
  return FindPerModule(context, [&identifier](const ModuleSymbols* ms) {
    return FindExactNameInModuleIndex(ms, identifier);
  });
}

FoundName FindExactNameInModuleIndex(const ModuleSymbols* module_symbols,
                                     const Identifier& identifier) {
  return FindPerIndexNode(module_symbols,
                          IndexWalker(&module_symbols->GetIndex()), identifier);
}

}  // namespace zxdb
