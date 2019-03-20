// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/bin/zxdb/expr/identifier.h"
#include "garnet/bin/zxdb/symbols/type.h"

namespace zxdb {

struct NameLookupResult {
  // Since identifiers with template parameters at the end are assumed to be
  // a type, we don't need to check that "std::vector<int>" is a type. This
  // will need to be revisited if we support templatized function names in
  // expressions ("auto a = &MyClass::MyFunc<int>;");
  enum Kind {
    kNamespace,  // Namespace name like "std".
    kTemplate,   // Template name without parameters like "std::vector".
    kType,       // Full type name like "std::string" or "int".
    kOther,      // e.g. "Foo", or "std::string::npos".
  };

  NameLookupResult() = default;
  explicit NameLookupResult(Kind k, fxl::RefPtr<Type> t = nullptr)
      : kind(k), type(std::move(t)) {}

  Kind kind = kOther;
  fxl::RefPtr<Type> type;  // Valid when kind == kType.
};

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
using NameLookupCallback = std::function<NameLookupResult(const Identifier&)>;

}  // namespace zxdb
