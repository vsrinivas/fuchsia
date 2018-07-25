// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/namespace.h"

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"

namespace zxdb {

Namespace::Namespace() : Symbol(kTagNamespace) {}
Namespace::~Namespace() = default;

const Namespace* Namespace::AsNamespace() const { return this; }

std::string Namespace::ComputeFullName() const {
  const std::string& assigned = GetAssignedName();
  if (assigned.empty())
    return GetSymbolScopePrefix(this) + "(anon)";
  return GetSymbolScopePrefix(this) + assigned;
}

}  // namespace zxdb
