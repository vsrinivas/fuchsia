// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

std::string IdentifierComponent::GetName(bool include_debug) const {
  if (!include_debug && special_ == SpecialIdentifier::kNone)  // Common case.
    return name_;

  std::string result;
  if (include_debug)
    result.push_back('"');

  if (special_ == SpecialIdentifier::kNone) {
    result.append(name_);
  } else {
    result.append(SpecialIdentifierToString(special_));
    if (SpecialIdentifierHasData(special_)) {
      result.push_back('(');
      result.append(name_);
      result.push_back(')');
    }
  }

  if (include_debug)
    result.push_back('"');
  return result;
}

}  // namespace zxdb
