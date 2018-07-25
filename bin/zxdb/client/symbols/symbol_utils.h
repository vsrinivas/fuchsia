// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

// This helper function gets the scope for the symbol (GetSymboLScope() above),
// and converts it to a string (SymbolScopeToPrefixString() above).
std::string GetSymbolScopePrefix(const Symbol* symbol);

}  // namespace zxdb
