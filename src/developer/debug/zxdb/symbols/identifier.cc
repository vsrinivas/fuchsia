// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

std::string IdentifierComponent::GetName(bool include_debug) const {
  std::string result;

  if (include_debug)
    result.push_back('"');
  result += name_;
  if (include_debug)
    result.push_back('"');

  if (has_template()) {
    if (include_debug)
      result.push_back(',');
    result.push_back('<');

    for (size_t i = 0; i < template_contents().size(); i++) {
      if (i > 0)
        result += ", ";

      // Template parameter string.
      if (include_debug)
        result.push_back('"');
      result += template_contents()[i];
      if (include_debug)
        result.push_back('"');
    }
    result.push_back('>');
  }
  return result;
}

}  // namespace zxdb
