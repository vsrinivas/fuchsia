// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/pretty_string.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/expr/pretty_vector.h"

namespace zxdb {

PrettyType* PrettyTypeManager::GetForType(const Type* type) const {
  if (!type)
    return nullptr;

  // TODO(brettw) this is currently hardcoded but needs some kind of registry with globs or
  // something that match type names to objects.
  const std::string& type_name = type->GetFullName();

  if (type->GetLanguage() == DwarfLang::kRust) {
    if (type_name == "&str") {
      static PrettyRustStr pretty_rust_str;
      return &pretty_rust_str;
    } else if (type_name == "alloc::string::String") {
      static PrettyRustString pretty_rust_string;
      return &pretty_rust_string;
    } else if (StringBeginsWith(type_name, "alloc::vec::Vec<")) {
      static PrettyArray pretty_rust_vec("buf.ptr.pointer", "len");
      return &pretty_rust_vec;
    }
  } else {
    // Assume C/C++ for everything else.

    if (type_name ==
            "std::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >" ||
        type_name == "std::__2::string") {
      // Because of the weirdness of std::string's definition, we need to check for both the typedef
      // source and the resolved value. The typedef won't always map to something.
      static PrettyStdString pretty_std_string;
      return &pretty_std_string;
    } else if (type_name == "std::__2::basic_string_view<char, std::__2::char_traits<char> >") {
      static PrettyStdStringView pretty_std_string_view;
      return &pretty_std_string_view;
    } else if (StringBeginsWith(type_name, "std::__2::vector<") &&
               !StringBeginsWith(type_name, "std::__2::vector<bool,")) {
      static PrettyStdVector pretty_std_vector;
      return &pretty_std_vector;
    }
  }

  return nullptr;
}

bool PrettyTypeManager::Format(FormatNode* node, const Type* type, const FormatOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback& cb) const {
  if (PrettyType* pretty = GetForType(type)) {
    pretty->Format(node, options, context, std::move(cb));
    return true;
  }
  return false;
}

}  // namespace zxdb
