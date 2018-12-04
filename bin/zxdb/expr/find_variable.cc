// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/find_variable.h"

#include "garnet/bin/zxdb/expr/found_variable.h"
#include "garnet/bin/zxdb/expr/identifier.h"
#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/type_utils.h"
#include "garnet/bin/zxdb/symbols/visit_scopes.h"

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

}  // namespace

std::optional<FoundVariable> FindVariable(const CodeBlock* block,
                                          const Identifier& identifier) {
  if (block) {
    // Search for local variables and function parameters.
    if (auto found = FindLocalVariable(block, identifier))
      return found;

    // Search the "this" object.
    if (auto found = FindMemberOnThis(block, identifier))
      return found;
  }

  // Fall back to searching global vars.
  // TODO(brettw) implement this.
  //   FindGlobalVariable(name)...
  return std::nullopt;
}

std::optional<FoundVariable> FindLocalVariable(const CodeBlock* block,
                                               const Identifier& identifier) {
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
      return FoundVariable(var);

    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (auto* var = SearchVariableVector(function->parameters(), *name))
        return FoundVariable(var);
      break;  // Don't recurse into higher levels of nesting than a function.
    }
    if (!cur_block->parent())
      break;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }
  return std::nullopt;
}

std::optional<FoundMember> FindMember(const Collection* object,
                                      const Identifier& identifier) {
  // TODO(brettw) allow "BaseClass::foo" syntax for specifically naming a
  // member of a base class. Watch out: the base class could be qualified (or
  // not) in various ways: ns::BaseClass::foo, BaseClass::foo, etc.
  const std::string* ident_name = identifier.GetSingleComponentName();
  if (!ident_name)
    return std::nullopt;

  // This code will check the object and all base classes.
  std::optional<FoundMember> result;
  VisitClassHierarchy(
      object, [ident_name, &result](const Collection* cur_collection,
                                     uint32_t cur_offset) -> bool {
        // Called for each collection in the hierarchy.
        for (const auto& lazy : cur_collection->data_members()) {
          const DataMember* data = lazy.Get()->AsDataMember();
          if (data && data->GetAssignedName() == *ident_name) {
            result.emplace(data, cur_offset + data->member_location());
            return true;
          }
        }
        return false;  // Not found in this scope, continue search.
      });
  return result;
}

std::optional<FoundVariable> FindMemberOnThis(const CodeBlock* block,
                                              const Identifier& identifier) {
  // Find the function to see if it has a |this| pointer.
  const Function* function = block->GetContainingFunction();
  if (!function || !function->object_pointer())
    return std::nullopt;  // No "this" pointer.

  // The "this" variable.
  const Variable* this_var = function->object_pointer().Get()->AsVariable();
  if (!this_var)
    return std::nullopt;  // Symbols likely corrupt.

  // Pointed-to type for "this".
  const Collection* collection = nullptr;
  if (GetPointedToCollection(this_var->type().Get()->AsType(), &collection)
          .has_error())
    return std::nullopt;  // Symbols likely corrupt.

  if (auto member = FindMember(collection, identifier))
    return FoundVariable(this_var, std::move(*member));
  return std::nullopt;
}

}  // namespace zxdb
