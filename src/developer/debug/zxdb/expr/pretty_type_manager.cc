// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/pretty_std_string.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"

namespace zxdb {

PrettyType* PrettyTypeManager::GetForType(const Type* type) const {
  if (!type)
    return nullptr;

  // TODO(brettw) this is currently hardcoded but needs some kind of registry with globs or
  // something that match type names to objects.
  const std::string& type_name = type->GetFullName();

  // std::string.
  const char kStdStringLongName[] =
      "basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >";
  const char kStdStringShortName[] = "std::__2::string";
  if (type_name == kStdStringLongName || type_name == kStdStringShortName) {
    static PrettyStdString pretty_std_string;
    return &pretty_std_string;
  }

  return nullptr;
}

bool PrettyTypeManager::Format(FormatNode* node, const FormatOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback& cb) const {
  if (PrettyType* pretty = GetForType(node->value().type())) {
    pretty->Format(node, options, context, std::move(cb));
    return true;
  }
  return false;
}

}  // namespace zxdb
