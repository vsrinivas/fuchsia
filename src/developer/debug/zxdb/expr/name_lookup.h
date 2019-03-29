// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/identifier.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Looks up the given identifier in the current evaluation context and
// determines the type of identifier it is.
//
// As noted in the documentation for "Kind" above, the input identifier will
// never have template parameters. It will always have a name by itself as the
// last component.
//
// NOTE: This isn't quite correct C++ for cases where the argument can be
// either a type name or a variable. This happens with "sizeof(X)". The first
// thing (type or variable) matching "X" is used. With this API, we'll see if
// it could possibly be a type and always give the result for the type.
using NameLookupCallback = std::function<FoundName(const Identifier&)>;

}  // namespace zxdb
