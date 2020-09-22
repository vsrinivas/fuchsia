// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/find_name.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/index_walker.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/index_node.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

namespace {

// FindName doesn't support every type of input name.
enum class FindNameSupported {
  // Normal symbol completely supported by FindName.
  kFully,

  // Query the module symbols for this name. Used for names not in the index like ELF symbols.
  kModuleSymbolsOnly,

  // This name can't use FindName. For example, registers can't be looked up.
  kNo,
};
FindNameSupported GetSupported(const ParsedIdentifier& identifier) {
  for (const auto& comp : identifier.components()) {
    switch (comp.special()) {
      case SpecialIdentifier::kNone:
      case SpecialIdentifier::kAnon:
        break;  // Normal boring component.
      case SpecialIdentifier::kEscaped:
      case SpecialIdentifier::kLast:
        FX_NOTREACHED();  // These annotations shouldn't appear in identifiers.
        break;
      case SpecialIdentifier::kMain:
      case SpecialIdentifier::kElf:
      case SpecialIdentifier::kPlt:
        // These symbols are queried directly from ModuleSymbols.
        return FindNameSupported::kModuleSymbolsOnly;
      case SpecialIdentifier::kRegister:
        return FindNameSupported::kNo;  // Can't look up registers in the symbols.
    }
  }
  return FindNameSupported::kFully;
}

// Returns true if an index search is required for the options. Everything but local variables
// requires the index.
bool OptionsRequiresIndex(const FindNameOptions& options) {
  return options.find_types || options.find_type_defs || options.find_functions ||
         options.find_templates || options.find_namespaces;
}

// Returns true if the |name| of an object matches what we're |looking_for| given the current
// options.
bool NameMatches(const FindNameOptions& options, const std::string& name,
                 const std::string& looking_for) {
  if (options.how == FindNameOptions::kPrefix)
    return StringBeginsWith(name, looking_for);
  return name == looking_for;
}

// Iterates over the variables in the given vector, calling the visit callback for each as long as
// the visitor says to continue. Searches the given vector of values for one with the given name. If
// found, returns it, otherwise returns null.
VisitResult VisitVariableVector(const std::vector<LazySymbol>& vect,
                                fit::function<VisitResult(const Variable*)>& visitor) {
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

FoundName FoundNameFromSymbolRef(const ModuleSymbols* module_symbols,
                                 const FindNameOptions& options, const IndexNode::SymbolRef& ref) {
  LazySymbol lazy_symbol = module_symbols->IndexSymbolRefToSymbol(ref);
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

  if (const DataMember* dm = symbol->AsDataMember()) {
    FX_DCHECK(dm->is_external());  // Only static ("external") members should be in the index.
    if (options.find_vars)
      return FoundName(nullptr, FoundMember(nullptr, dm));
    return FoundName();
  }

  if (const Namespace* ns = symbol->AsNamespace()) {
    if (options.find_namespaces)
      return FoundName(FoundName::kNamespace, ns->GetFullName());
    return FoundName();
  }

  if (const Type* type = symbol->AsType()) {
    if (options.find_types)  // All types.
      return FoundName(RefPtrTo(type));
    if (options.find_type_defs && !type->is_declaration())  // Type definitions only.
      return FoundName(RefPtrTo(type));
    return FoundName();
  }

  return FoundName();
}

VisitResult GetNamesFromDieList(const ModuleSymbols* module_symbols, const FindNameOptions& options,
                                const std::vector<IndexNode::SymbolRef>& dies,
                                std::vector<FoundName>* results) {
  for (const IndexNode::SymbolRef& cur : dies) {
    if (FoundName found = FoundNameFromSymbolRef(module_symbols, options, cur))
      results->push_back(std::move(found));

    if (results->size() >= options.max_results)
      return VisitResult::kDone;
  }
  return VisitResult::kContinue;
}

// Finds the things matching the given prefix in the map of the index node. This map will correspond
// to indexed symbols of a given kind (functions, types, namespaces, etc.).
VisitResult AddPrefixesFromMap(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                               const IndexNode::Map& map, const std::string& prefix,
                               std::vector<FoundName>* results) {
  auto cur = map.lower_bound(prefix);
  while (cur != map.end() && NameMatches(options, cur->first, prefix)) {
    VisitResult vr = GetNamesFromDieList(module_symbols, options, cur->second.dies(), results);
    if (vr != VisitResult::kContinue)
      return vr;

    ++cur;
  }
  return VisitResult::kContinue;
}

// Adds the matches from the given node. The walker's current position should already match the name
// of the thing we're looking for.
VisitResult AddMatches(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                       const IndexWalker& walker, const ParsedIdentifier& looking_for,
                       std::vector<FoundName>* results) {
  // Namespaces are special because they don't store any DIEs. If we're looking for a namespace
  // need to add the current node name.
  if (options.find_namespaces) {
    for (const auto& current_node : walker.current()) {
      // Got a namespace with the name.
      if (current_node->kind() == IndexNode::Kind::kNamespace) {
        results->emplace_back(FoundName::kNamespace, looking_for);
        if (results->size() >= options.max_results)
          return VisitResult::kDone;
        break;
      }
    }
  }

  // Check for things that have DIEs. Note that "templates" isn't included in this list because
  // those are treated separately (they're a prefix search on a type).
  if (options.find_types || options.find_type_defs || options.find_functions || options.find_vars) {
    for (const auto& current_node : walker.current()) {
      VisitResult vr = GetNamesFromDieList(module_symbols, options, current_node->dies(), results);
      if (vr != VisitResult::kContinue)
        return vr;
    }
  }

  return VisitResult::kContinue;
}

// Given a scope, finds all things inside of it that match the prefix (the last component of
// "looking_for") and adds them to the results.
VisitResult AddPrefixes(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                        const IndexWalker& scope, const ParsedIdentifier& looking_for,
                        std::vector<FoundName>* results) {
  std::string prefix = looking_for.components().back().GetName(false);

  // Check all nodes representing this scope (there could be multiple paths in the index
  // corresponding to symbols of different kinds).
  for (const auto& current_node : scope.current()) {
    // Depending on the kind of thing the caller is interested in, we only need to look at
    // certain parts of each node.
    if (options.find_types || options.find_templates || options.find_type_defs) {
      VisitResult vr =
          AddPrefixesFromMap(options, module_symbols, current_node->types(), prefix, results);
      if (vr != VisitResult::kContinue)
        return vr;
    }

    if (options.find_functions) {
      VisitResult vr =
          AddPrefixesFromMap(options, module_symbols, current_node->functions(), prefix, results);
      if (vr != VisitResult::kContinue)
        return vr;
    }

    if (options.find_vars) {
      VisitResult vr =
          AddPrefixesFromMap(options, module_symbols, current_node->vars(), prefix, results);
      if (vr != VisitResult::kContinue)
        return vr;
    }

    if (options.find_namespaces) {
      // Namespaces get special handling because DIEs are not actually stored for them, just
      // a "namespace" IndexNode.
      auto cur = current_node->namespaces().lower_bound(prefix);
      while (cur != current_node->namespaces().end() && NameMatches(options, cur->first, prefix)) {
        // Compute the full name of this namespace.
        ParsedIdentifier full_name = looking_for.GetScope();
        full_name.AppendComponent(ParsedIdentifierComponent(cur->first));

        results->emplace_back(FoundName::kNamespace, std::move(full_name));
        if (results->size() >= options.max_results)
          return VisitResult::kDone;
        ++cur;
      }
    }
  }

  return VisitResult::kContinue;
}

VisitResult VisitPerModule(const FindNameContext& context,
                           fit::function<VisitResult(const ModuleSymbols*)> visitor) {
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

// Searches for |looking_for| at a given level of the index, as stored in the given IndexWalker.
VisitResult FindInIndexLevel(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                             const IndexWalker& walker, const ParsedIdentifier& looking_for,
                             std::vector<FoundName>* results) {
  if (looking_for.empty())
    return VisitResult::kDone;

  ParsedIdentifier looking_for_scope = looking_for.GetScope();

  // Walk into all but the last node of the identifier (the last one is the part that needs
  // completion).
  IndexWalker scope_walker(walker);
  if (!scope_walker.WalkInto(looking_for_scope))
    return VisitResult::kContinue;

  // Need to separate out prefix so we can take advantage of the template canonicalization of the
  // IndexWalker in the exact match case. This means that we can't currently do prefix matches of
  // templates that are canonicalized differently than DWARF represents them.
  if (options.how == FindNameOptions::kPrefix) {
    VisitResult vr = AddPrefixes(options, module_symbols, scope_walker, looking_for, results);
    if (vr != VisitResult::kContinue)
      return vr;
  } else {
    // Exact match case.
    //
    // TODO(brettw) in cases where we know the exact type of the thing we're looking for (e.g.
    // "namespaces") we could optimize by adding a way for the walker to only go into that kind of
    // child IndexNode.
    if (scope_walker.WalkInto(looking_for.components().back())) {
      VisitResult vr = AddMatches(options, module_symbols, scope_walker, looking_for, results);
      if (vr != VisitResult::kContinue)
        return vr;

      // Undo the walk we just made so we can search for templates below using the same scope.
      scope_walker.WalkUp();
    }
  }

  // We also want to know if there are any templates with that name which will look like
  // "foo::bar<...". In that case, do a prefix search with an appended "<" and see if there are any
  // results. Don't bother if the input already has a template.
  //
  // General prefix matches and non-template queries (if also included) will already have been
  // caught above so don't handle here.
  if (options.how == FindNameOptions::kExact && options.find_templates &&
      !looking_for.components().back().has_template()) {
    // This is the prefix for the type we look for to find the template.
    std::string prefix = looking_for.components().back().GetName(false);
    prefix.push_back('<');

    // Check for types in each node at this scope for prefix matches. If any of them match, return
    // one. We don't need to return all of them since a template query just returns whether a
    // template of that name exists (each specialization is a "type" instead).
    for (const auto& current_node : scope_walker.current()) {
      auto found = current_node->types().lower_bound(prefix);
      // Note: need StringBeginsWith rather than NameMatches since we always want to do a prefix
      // search rather than applying the current prefix/exact mode from the options.
      if (found != current_node->types().end() && StringBeginsWith(found->first, prefix)) {
        results->emplace_back(FoundName::kTemplate, looking_for.GetFullName());
        if (results->size() >= options.max_results)
          return VisitResult::kDone;  // Don't need to look for anything else.

        // Don't need to look for more template matches but may need to continue the search
        // for other stuff.
        break;
      }
    }
  }

  return VisitResult::kContinue;
}

// Searches the given index node and recursively, all child namespaces. This is used to implement
// the "all namespaces" search mode.
VisitResult FindInIndexLevelRecursiveNs(const FindNameOptions& options,
                                        const ModuleSymbols* module_symbols,
                                        const IndexWalker& walker,
                                        const ParsedIdentifier& looking_for,
                                        std::vector<FoundName>* results) {
  // Search in this node first.
  VisitResult vr = FindInIndexLevel(options, module_symbols, walker, looking_for, results);
  if (vr != VisitResult::kContinue)
    return vr;

  // Recursively search in child namespaces.
  IndexWalker ns_walker(walker);  // Stores the namespace we're checking.
  for (const auto& current_node : walker.current()) {
    for (const auto& [ns_name, ns_node] : current_node->namespaces()) {
      // Check one specific child namespace index node.
      ns_walker.WalkIntoSpecific(&ns_node);
      vr = FindInIndexLevelRecursiveNs(options, module_symbols, ns_walker, looking_for, results);
      if (vr != VisitResult::kContinue)
        return vr;
      ns_walker.WalkUp();
    }
  }
  return VisitResult::kContinue;
}

// Searches a specific collection for a data member with the given |looking_for| name. This is
// a helper for FindMember that searches one level.
//
// This takes one additional parameter over FindMember: the |cur_offset| which is the offset of
// the current collection being iterated over in whatever contains it.
VisitResult FindMemberOn(const FindNameContext& context, const FindNameOptions& options,
                         const InheritancePath& path, const ParsedIdentifier& looking_for,
                         const Variable* optional_object_ptr, std::vector<FoundName>* result) {
  auto base = GetConcreteType(context, path.base());
  const Collection* base_coll = base->AsCollection();
  if (!base_coll)
    return VisitResult::kContinue;  // Nothing to do at this level.

  // Data member iteration.
  if (const std::string* looking_for_name = GetSingleComponentIdentifierName(looking_for);
      looking_for_name && options.find_vars) {
    for (const auto& lazy : base_coll->data_members()) {
      if (const DataMember* data = lazy.Get()->AsDataMember()) {
        // TODO(brettw) allow "BaseClass::foo" syntax for specifically naming a member of a base
        // class. Watch out: the base class could be qualified (or not) in various ways:
        // ns::BaseClass::foo, BaseClass::foo, etc.
        if (NameMatches(options, data->GetAssignedName(), *looking_for_name)) {
          result->emplace_back(optional_object_ptr, path, data);
          if (result->size() >= options.max_results)
            return VisitResult::kDone;
        }

        // Check for anonymous unions.
        if (data->GetAssignedName().empty()) {
          // Recursively search into anonymous unions. We assume this is C++ and anonymous
          // collections can't have base classes so we don't need to VisitClassHierarchy().
          if (const Collection* member_coll = data->type().Get()->AsCollection()) {
            // Construct a new inheritance path with a synthetic InheritedFrom member to represent
            // the offset of the anonymous collection within the containing one.
            InheritancePath synthetic_path(path);
            synthetic_path.path().emplace_back(
                fxl::MakeRefCounted<InheritedFrom>(RefPtrTo(member_coll), data->member_location()),
                RefPtrTo(member_coll));

            VisitResult visit_result = FindMemberOn(context, options, synthetic_path, looking_for,
                                                    optional_object_ptr, result);
            if (visit_result != VisitResult::kContinue)
              return visit_result;
          }
        }
      }
    }  // for (data_members)
  }

  // Index node iteration for this class' scope.
  if (OptionsRequiresIndex(options)) {
    ParsedIdentifier container_name = ToParsedIdentifier(base_coll->GetIdentifier());

    // Don't search previous scopes (pass |search_containing| = false). If a class derives from a
    // class in another namespace, that doesn't bring the other namespace in the current scope.
    VisitResult vr = FindIndexedName(context, options, container_name, looking_for, false, result);
    if (vr != VisitResult::kContinue)
      return vr;
  }

  return VisitResult::kContinue;
}

}  // namespace

FindNameContext::FindNameContext(const ProcessSymbols* ps, const SymbolContext& symbol_context,
                                 const CodeBlock* b)
    : block(b) {
  if (ps) {
    target_symbols = ps->target_symbols();

    if (!symbol_context.is_relative()) {
      // Valid symbol context was given, try to find the corresponding module.
      uint64_t module_load_address = symbol_context.RelativeToAbsolute(0);
      for (const LoadedModuleSymbols* mod : ps->GetLoadedModuleSymbols()) {
        if (mod->load_address() == module_load_address) {
          module_symbols = mod->module_symbols();
          break;
        }
      }
    }
  }
}

FindNameContext::FindNameContext(const TargetSymbols* ts) : target_symbols(ts) {}

FoundName FindName(const FindNameContext& context, const FindNameOptions& options,
                   const ParsedIdentifier& identifier) {
  FindNameOptions new_opts(options);
  new_opts.max_results = 1;

  std::vector<FoundName> results;
  FindName(context, new_opts, identifier, &results);
  if (!results.empty())
    return std::move(results[0]);
  return FoundName();
}

void FindName(const FindNameContext& context, const FindNameOptions& options,
              const ParsedIdentifier& looking_for, std::vector<FoundName>* results) {
  FindNameSupported supported = GetSupported(looking_for);
  if (supported == FindNameSupported::kNo)
    return;  // Nothing to do for these symbols.

  // This only works for fully-supported identifier types. Some work only with the module-specific
  // symbol query we do below.
  if (supported == FindNameSupported::kFully && options.search_mode == FindNameOptions::kLexical &&
      options.find_vars && context.block &&
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
    // Get the scope for the current function. This may fail in which case we'll be left with an
    // empty current scope. This is non-fatal: it just means we won't implicitly search the current
    // namespace and will search only the global one.
    ParsedIdentifier current_scope;
    if (context.block) {
      if (auto function = context.block->GetContainingFunction()) {
        current_scope = ToParsedIdentifier(function->GetIdentifier()).GetScope();
      }
    }
    FindIndexedName(context, options, current_scope, looking_for, true, results);
  }
}

VisitResult VisitLocalVariables(const CodeBlock* block,
                                fit::function<VisitResult(const Variable*)> visitor) {
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
                       const ParsedIdentifier& looking_for, std::vector<FoundName>* results) {
  FX_DCHECK(options.search_mode == FindNameOptions::kLexical);

  // TODO(fxbug.dev/6038) lookup type names defined locally in this function.

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
                const Variable* optional_object_ptr, std::vector<FoundName>* result) {
  FX_DCHECK(options.search_mode == FindNameOptions::kLexical);

  VisitClassHierarchy(object, [&context, &options, &looking_for, optional_object_ptr,
                               result](const InheritancePath& path) {
    // Called for each collection in the class hierarchy.
    return FindMemberOn(context, options, path, looking_for, optional_object_ptr, result);
  });
}

void FindMemberOnThis(const FindNameContext& context, const FindNameOptions& options,
                      const ParsedIdentifier& looking_for, std::vector<FoundName>* result) {
  FX_DCHECK(options.search_mode == FindNameOptions::kLexical);

  if (!context.block)
    return;  // No current code.
  auto function = context.block->GetContainingFunction();
  if (!function)
    return;
  const Variable* this_var = function->GetObjectPointerVariable();
  if (!this_var)
    return;  // No "this" pointer.

  // Type for "this".
  fxl::RefPtr<Type> this_type = GetConcreteType(context, this_var->type().Get()->AsType());
  if (!this_type)
    return;  // Bad type.

  const ModifiedType* modified = this_type->AsModifiedType();
  if (!modified || modified->tag() != DwarfTag::kPointerType)
    return;  // Not a pointer.

  const Collection* this_coll = modified->modified().Get()->AsCollection();
  if (!this_coll)
    return;  // "this" is not a collection, probably corrupt.

  FindMember(context, options, this_coll, looking_for, this_var, result);
}

VisitResult FindIndexedName(const FindNameContext& context, const FindNameOptions& options,
                            const ParsedIdentifier& current_scope,
                            const ParsedIdentifier& looking_for, bool search_containing,
                            std::vector<FoundName>* results) {
  return VisitPerModule(context, [&options, &current_scope, &looking_for, search_containing,
                                  results](const ModuleSymbols* ms) {
    FindIndexedNameInModule(options, ms, current_scope, looking_for, search_containing, results);
    return results->size() >= options.max_results ? VisitResult::kDone : VisitResult::kContinue;
  });
}

void FindIndexedNameInModule(const FindNameOptions& options, const ModuleSymbols* module_symbols,
                             const ParsedIdentifier& current_scope,
                             const ParsedIdentifier& looking_for, bool search_containing,
                             std::vector<FoundName>* results) {
  if (GetSupported(looking_for) == FindNameSupported::kModuleSymbolsOnly) {
    // These symbols can only be looked up by ModuleSymbols and aren't in the normal index. Defer
    // to the symbol system for these lookups.
    for (const Location& loc : module_symbols->ResolveInputLocation(
             SymbolContext::ForRelativeAddresses(), InputLocation(ToIdentifier(looking_for)))) {
      results->emplace_back(loc.symbol().Get());
    }
    return;
  }

  IndexWalker walker(&module_symbols->GetIndex());
  if (options.search_mode == FindNameOptions::kLexical && !current_scope.empty() &&
      looking_for.qualification() == IdentifierQualification::kRelative) {
    // Unless the input identifier is fully qualified, start the search in the current context.
    walker.WalkIntoClosest(current_scope);
  }

  // Search from the current namespace going up.
  do {
    // Do recursive searching when requested. The name must also be relative. Global qualifications
    // on the input override implicit namespace searching.
    VisitResult vr;
    if (options.search_mode == FindNameOptions::kAllNamespaces &&
        looking_for.qualification() == IdentifierQualification::kRelative) {
      vr = FindInIndexLevelRecursiveNs(options, module_symbols, walker, looking_for, results);
    } else {
      vr = FindInIndexLevel(options, module_symbols, walker, looking_for, results);
    }
    if (vr != VisitResult::kContinue)
      return;
    if (!search_containing)
      break;

    // Keep looking up one more level in the containing namespace.
  } while (walker.WalkUp());
}

const std::string* GetSingleComponentIdentifierName(const ParsedIdentifier& ident) {
  if (ident.components().size() != 1 || ident.components()[0].has_template() ||
      ident.components()[0].special() != SpecialIdentifier::kNone)
    return nullptr;
  return &ident.components()[0].name();
}

}  // namespace zxdb
