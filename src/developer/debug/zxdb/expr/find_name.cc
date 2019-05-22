// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/find_name.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/index_walker.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index_node.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/developer/debug/zxdb/symbols/type_utils.h"

namespace zxdb {

namespace {

// Returns true if an index search is required for the options. Everything but
// local variables requires the index.
bool OptionsRequiresIndex(const FindNameOptions& options) {
  return options.find_types || options.find_functions ||
         options.find_templates || options.find_namespaces;
}

// Returns true if the |name| of an object matches what we're |looking_for|
// given the current options.
bool NameMatches(const FindNameOptions& options, const std::string& name,
                 const std::string& looking_for) {
  if (options.how == FindNameOptions::kPrefix)
    return StringBeginsWith(name, looking_for);
  return name == looking_for;
}

// Iterates over the variables in the given vector, calling the visit callback
// for each as long as the visitor says to continue.
// Searches the given vector of values for one with the given name. If found,
// returns it, otherwise returns null.
VisitResult VisitVariableVector(
    const std::vector<LazySymbol>& vect,
    const std::function<VisitResult(const Variable*)>& visitor) {
  for (const auto& cur : vect) {
    const Variable* var = cur.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.

    VisitResult vr = visitor(var);
    if (vr != VisitResult::kContinue)
      return vr;
  }
  return VisitResult::kContinue;
}

FoundName FoundNameFromDieRef(const ModuleSymbols* module_symbols,
                              const FindNameOptions& options,
                              const ModuleSymbolIndexNode::DieRef& ref) {
  LazySymbol lazy_symbol = module_symbols->IndexDieRefToSymbol(ref);
  if (!lazy_symbol)
    return FoundName();
  const Symbol* symbol = lazy_symbol.Get();

  if (const Function* func = symbol->AsFunction()) {
    if (options.find_functions)
      return FoundName(func);
    return FoundName();
  }

  if (const Variable* var = symbol->AsVariable()) {
    if (options.find_vars)
      return FoundName(var);
    return FoundName();
  }

  if (const Namespace* ns = symbol->AsNamespace()) {
    if (options.find_namespaces)
      return FoundName(FoundName::kNamespace, ns->GetFullName());
    return FoundName();
  }

  if (const Type* type = symbol->AsType()) {
    if (options.find_types)
      return FoundName(fxl::RefPtr<Type>(const_cast<Type*>(type)));
    return FoundName();
  }

  return FoundName();
}

VisitResult GetNamesFromDieList(
    const ModuleSymbols* module_symbols, const FindNameOptions& options,
    const std::vector<ModuleSymbolIndexNode::DieRef>& dies,
    std::vector<FoundName>* results) {
  for (const ModuleSymbolIndexNode::DieRef& cur : dies) {
    if (FoundName found = FoundNameFromDieRef(module_symbols, options, cur))
      results->push_back(std::move(found));

    if (results->size() >= options.max_results)
      return VisitResult::kDone;
  }
  return VisitResult::kContinue;
}

VisitResult VisitPerModule(
    const FindNameContext& context,
    std::function<VisitResult(const ModuleSymbols*)> visitor) {
  if (context.module_symbols) {
    // Search in the current module.
    VisitResult vr = visitor(context.module_symbols);
    if (vr != VisitResult::kContinue)
      return vr;
  }

  // Search in all other modules as a fallback, if any.
  if (context.target_symbols) {
    for (const ModuleSymbols* m : context.target_symbols->GetModuleSymbols()) {
      if (m != context.module_symbols) {  // Don't re-search current one.
        VisitResult vr = visitor(m);
        if (vr != VisitResult::kContinue)
          return vr;
      }
    }
  }

  return VisitResult::kContinue;
}

VisitResult FindPerIndexNode(const FindNameOptions& options,
                             const ModuleSymbols* module_symbols,
                             const IndexWalker& walker,
                             const ParsedIdentifier& looking_for,
                             std::vector<FoundName>* results) {
  if (looking_for.empty())
    return VisitResult::kDone;

  ParsedIdentifier looking_for_scope = looking_for.GetScope();

  // Walk into all but the last node of the identifier (the last one is
  // the part that needs completion).
  IndexWalker prefix_walker(walker);
  if (!prefix_walker.WalkInto(looking_for_scope))
    return VisitResult::kContinue;

  // Now search that node for anything with the given prefix. This also handles
  // the exact match case because an exact match will be the first thing found
  // with a prefix search. NameMatches() will handle the difference.
  std::string last_comp = looking_for.components().back().GetName(false);
  auto [cur, end] = prefix_walker.current()->FindPrefix(last_comp);
  while (cur != end && results->size() < options.max_results &&
         NameMatches(options, cur->first, last_comp)) {
    VisitResult vr = GetNamesFromDieList(module_symbols, options,
                                         cur->second.dies(), results);
    if (vr != VisitResult::kContinue)
      return vr;

    ++cur;
  }

  // We also want to know if there are any templates with that name which will
  // look like "foo::bar<...". In that case, do a prefix search with an
  // appended "<" and see if there are any results. Don't bother if the
  // input already has a template.
  //
  // Prefix matches will already have been caught above so don't handle here.
  if (options.how == FindNameOptions::kExact && options.find_templates &&
      !looking_for.components().back().has_template()) {
    // Walk into all but the last node of the identifier (the last one is
    // potentially the template).
    IndexWalker template_walker(walker);
    if (!template_walker.WalkInto(looking_for_scope))
      return VisitResult::kContinue;

    // Now search that node for anything that could be a template.
    std::string last_comp = looking_for.components().back().GetName(false);
    last_comp.push_back('<');
    auto [cur, end] = template_walker.current()->FindPrefix(last_comp);
    if (cur != end) {
      // We could check every possible match with this prefix and see if there's
      // one with a type name. But that seems unnecessary. Instead, assume that
      // anything with a name containing a "<" is a template type name.
      results->emplace_back(FoundName::kTemplate, looking_for.GetFullName());
      if (results->size() >= options.max_results)
        return VisitResult::kDone;
    }
  }

  return VisitResult::kContinue;
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
                   const ParsedIdentifier& identifier) {
  std::vector<FoundName> results;
  FindName(context, FindNameOptions(FindNameOptions::kAllKinds), identifier,
           &results);
  if (!results.empty())
    return std::move(results[0]);
  return FoundName();
}

void FindName(const FindNameContext& context, const FindNameOptions& options,
              const ParsedIdentifier& looking_for,
              std::vector<FoundName>* results) {
  if (options.find_vars && context.block &&
      looking_for.qualification() == IdentifierQualification::kRelative) {
    // Search for local variables and function parameters.
    FindLocalVariable(options, context.block, looking_for, results);
    if (results->size() >= options.max_results)
      return;

    // Search the "this" object.
    FindMemberOnThis(context, options, looking_for, results);
    if (results->size() >= options.max_results)
      return;
  }

  // Fall back to searching global vars.
  if (context.module_symbols || context.target_symbols) {
    // Get the scope for the current function. This may fail in which case
    // we'll be left with an empty current scope. This is non-fatal: it just
    // means we won't implicitly search the current namespace and will search
    // only the global one.
    ParsedIdentifier current_scope;
    if (context.block) {
      if (const Function* function = context.block->GetContainingFunction()) {
        current_scope =
            ToParsedIdentifier(function->GetIdentifier()).GetScope();
      }
    }
    FindIndexedName(context, options, current_scope, looking_for, true,
                    results);
  }
}

VisitResult VisitLocalVariables(
    const CodeBlock* block,
    const std::function<VisitResult(const Variable*)>& visitor) {
  return VisitLocalBlocks(block, [&visitor](const CodeBlock* cur_block) {
    // Local variables in this block.
    VisitResult vr = VisitVariableVector(cur_block->variables(), visitor);
    if (vr != VisitResult::kContinue)
      return vr;

    // Function parameters.
    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      vr = VisitVariableVector(function->parameters(), visitor);
      if (vr != VisitResult::kContinue)
        return vr;
    }
    return VisitResult::kContinue;
  });
}

void FindLocalVariable(const FindNameOptions& options, const CodeBlock* block,
                       const ParsedIdentifier& looking_for,
                       std::vector<FoundName>* results) {
  // TODO(DX-1214) lookup type names defined locally in this function.

  // Local variables can only be simple names.
  const std::string* name = GetSingleComponentIdentifierName(looking_for);
  if (!name)
    return;

  VisitLocalVariables(block, [&options, name, &results](const Variable* var) {
    if (NameMatches(options, var->GetAssignedName(), *name)) {
      results->emplace_back(var);
      if (results->size() >= options.max_results)
        return VisitResult::kDone;
    }
    return VisitResult::kContinue;
  });
}

void FindMember(const FindNameContext& context, const FindNameOptions& options,
                const Collection* object, const ParsedIdentifier& looking_for,
                const Variable* optional_object_ptr,
                std::vector<FoundName>* result) {
  VisitClassHierarchy(
      object, [&context, &options, &looking_for, optional_object_ptr, result](
                  const Collection* cur_collection, uint64_t cur_offset) {
        // Called for each collection in the hierarchy.

        // Data member iteration.
        if (const std::string* looking_for_name =
                GetSingleComponentIdentifierName(looking_for);
            looking_for_name && options.find_vars) {
          for (const auto& lazy : cur_collection->data_members()) {
            if (const DataMember* data = lazy.Get()->AsDataMember()) {
              // TODO(brettw) allow "BaseClass::foo" syntax for specifically
              // naming a member of a base class. Watch out: the base class
              // could be qualified (or not) in various ways:
              // ns::BaseClass::foo, BaseClass::foo, etc.
              if (NameMatches(options, data->GetAssignedName(),
                              *looking_for_name)) {
                result->emplace_back(optional_object_ptr, data,
                                     cur_offset + data->member_location());
                if (result->size() >= options.max_results)
                  return VisitResult::kDone;
              }
            }
          }
        }

        // Index node iteration for this class' scope.
        if (OptionsRequiresIndex(options)) {
          ParsedIdentifier container_name =
              ToParsedIdentifier(cur_collection->GetIdentifier());

          // Don't search previous scopes (pass |search_containing| = false).
          // If a class derives from a class in another namespace, that
          // doesn't bring the other namespace in the current scope.
          VisitResult vr = FindIndexedName(context, options, container_name,
                                           looking_for, false, result);
          if (vr != VisitResult::kContinue)
            return vr;
        }

        return VisitResult::kContinue;
      });
}

void FindMemberOnThis(const FindNameContext& context,
                      const FindNameOptions& options,
                      const ParsedIdentifier& looking_for,
                      std::vector<FoundName>* result) {
  if (!context.block)
    return;  // No current code.
  const Function* function = context.block->GetContainingFunction();
  if (!function)
    return;
  const Variable* this_var = function->GetObjectPointerVariable();
  if (!this_var)
    return;  // No "this" pointer.

  // Pointed-to type for "this".
  const Collection* collection = nullptr;
  if (GetPointedToCollection(this_var->type().Get()->AsType(), &collection)
          .has_error())
    return;  // Symbols likely corrupt.

  FindMember(context, options, collection, looking_for, this_var, result);
}

VisitResult FindIndexedName(const FindNameContext& context,
                            const FindNameOptions& options,
                            const ParsedIdentifier& current_scope,
                            const ParsedIdentifier& looking_for,
                            bool search_containing,
                            std::vector<FoundName>* results) {
  return VisitPerModule(
      context, [&options, &current_scope, &looking_for, search_containing,
                results](const ModuleSymbols* ms) {
        FindIndexedNameInModule(options, ms, current_scope, looking_for,
                                search_containing, results);
        return results->size() >= options.max_results ? VisitResult::kDone
                                                      : VisitResult::kContinue;
      });
}

VisitResult FindIndexedNameInModule(const FindNameOptions& options,
                                    const ModuleSymbols* module_symbols,
                                    const ParsedIdentifier& current_scope,
                                    const ParsedIdentifier& looking_for,
                                    bool search_containing,
                                    std::vector<FoundName>* results) {
  IndexWalker walker(&module_symbols->GetIndex());
  if (!current_scope.empty() &&
      looking_for.qualification() == IdentifierQualification::kRelative) {
    // Unless the input identifier is fully qualified, start the search in the
    // current context.
    walker.WalkIntoClosest(current_scope);
  }

  // Search from the current namespace going up.
  do {
    VisitResult vr =
        FindPerIndexNode(options, module_symbols, walker, looking_for, results);
    if (vr != VisitResult::kContinue)
      return vr;
    if (!search_containing)
      break;

    // Keep looking up one more level in the containing namespace.
  } while (walker.WalkUp());

  // Current search is done, but there still may be stuff left to find.
  return VisitResult::kContinue;
}

const std::string* GetSingleComponentIdentifierName(
    const ParsedIdentifier& ident) {
  if (ident.components().size() != 1 || ident.components()[0].has_template())
    return nullptr;
  return &ident.components()[0].name();
}

}  // namespace zxdb
