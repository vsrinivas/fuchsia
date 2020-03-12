// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/permissive_input_location.h"

#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

// Returns true if the identifier is a special name that' sunderstood by the symbol system but
// which won't be in the index.
//
// Currently these are PLT breakpoints (called "foo@plt" for the function "foo"), and the entrypoint
// (called "@main"). The @ sign is not a valid identifier character otherwise, so we key off of
// that.
//
// TODO(bug 5722): Remove handling for "@" when al callers are updated to use the new syntax.
bool IsSpecialSymbolName(const Identifier& ident) {
  return ident.components().size() == 1u &&
         (ident.components()[0].name().find('@') != std::string::npos ||
          ident.components()[0].special() != SpecialIdentifier::kNone);
}

}  // namespace

std::vector<InputLocation> ExpandPermissiveInputLocationNames(
    const FindNameContext& context, const std::vector<InputLocation>& input) {
  // Currently all users of this API need the same set of options. This can be moved to a parameter
  // of this function if needed.
  FindNameOptions opts(FindNameOptions::kNoKinds);
  opts.max_results = FindNameOptions::kAllResults;
  opts.search_mode = FindNameOptions::kAllNamespaces;
  opts.find_functions = true;
  opts.find_vars = true;

  std::vector<InputLocation> result;

  std::vector<FoundName> found;  // Keep outside to avoid reallocation.
  for (const auto& in : input) {
    if (in.type == InputLocation::Type::kName) {
      // Needs expansion.
      found.clear();

      if (IsSpecialSymbolName(in.name)) {
        // Pass special names through, don't look in the index because they won't be there.
        result.push_back(in);
      } else {
        FindName(context, opts, ToParsedIdentifier(in.name), &found);
        for (const auto& f : found)
          result.emplace_back(ToIdentifier(f.GetName()));
      }
    } else {
      // Not a symbolic name, the output is the same as the input.
      result.push_back(in);
    }
  }
  return result;
}

// An alternate implementation of this function could get the actual symbol objects from the
// FindName results (function, variable), and then do a symbol lookup on that to get the full
// InputLocation. Basically InputLocation would have another "symbol object" mode that would take
// a RefPtr<Symbol> to look up.
//
// The advantage of that implementation is that it saves the symbol name lookup when we go to the
// ResolveInputLocation() call. Not round-tripping through names also helps remove some potential
// ambiguity about what we're referring to if there are multiple matches.
//
// The disadvantage is that the implementation is more complicated, especially since symbol objects
// don't currently have any ModuleSymbol information associated with them.
//
// TODO(bug 37608) Revisit this design when symbols know their modules. This might make the above
// design more desirable.
std::vector<Location> ResolvePermissiveInputLocations(const ProcessSymbols* process_symbols,
                                                      const ResolveOptions& resolve_options,
                                                      const FindNameContext& context,
                                                      const std::vector<InputLocation>& input) {
  std::vector<Location> result;
  for (const auto& in : ExpandPermissiveInputLocationNames(context, input)) {
    std::vector<Location> inner_result = process_symbols->ResolveInputLocation(in, resolve_options);
    result.insert(result.end(), inner_result.begin(), inner_result.end());
  }
  return result;
}

}  // namespace zxdb
