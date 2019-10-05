// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PERMISSIVE_INPUT_LOCATION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PERMISSIVE_INPUT_LOCATION_H_

#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

class ProcessSymbols;
struct ResolveOptions;

// Normally names in InputLocation objects must match exactly for the symbol system to find it.
//
// A permissive input location is one that matches in the current context and all namespaces.
// Converting a permissive name to a list of exact name matches is done by FindName in the
// expression library since it understands the various scoping and searching rules for symbol names.

// Expands the names of the input location(s) to all possible exact globally qualified names.
// Non-symbol-name-based inputs will be unchanged.
std::vector<InputLocation> ExpandPermissiveInputLocationNames(
    const FindNameContext& context, const std::vector<InputLocation>& input);

// Expands the symbol names using ExpandPermissiveInputLocationNames() and resolves the resulting
// names.
std::vector<Location> ResolvePermissiveInputLocations(const ProcessSymbols* process_symbols,
                                                      const ResolveOptions& resolve_options,
                                                      const FindNameContext& context,
                                                      const std::vector<InputLocation>& input);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PERMISSIVE_INPUT_LOCATION_H_
