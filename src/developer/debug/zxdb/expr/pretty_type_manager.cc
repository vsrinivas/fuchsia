// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/pretty_std_string.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"

namespace zxdb {

PrettyTypeManager::PrettyTypeManager() {
  AddDefaultCppPrettyTypes();
  AddDefaultRustPrettyTypes();
}

PrettyTypeManager::~PrettyTypeManager() = default;

PrettyType* PrettyTypeManager::GetForType(const Type* type) const {
  if (!type)
    return nullptr;
  const std::string& type_name = type->GetFullName();

  // Pick the language-specific lookup.
  const auto* lookup = type->GetLanguage() == DwarfLang::kRust ? &rust_ : &cpp_;

  // Tracks the longest one found so far.
  size_t longest_length = 0;
  PrettyType* longest_type = nullptr;

  for (const auto& [prefix, pretty_ptr] : *lookup) {
    if (StringBeginsWith(type_name, prefix) && prefix.size() > longest_length) {
      // Got a new best match.
      longest_length = prefix.size();
      longest_type = pretty_ptr.get();
    }
  }

  return longest_type;
}

bool PrettyTypeManager::Format(FormatNode* node, const Type* type, const FormatOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback& cb) const {
  if (PrettyType* pretty = GetForType(type)) {
    pretty->Format(node, options, context, std::move(cb));
    return true;
  }
  return false;
}

void PrettyTypeManager::AddDefaultCppPrettyTypes() {
  // std::string
  //
  // Because of the weirdness of std::string's definition, we need to check for both the typedef
  // source and the resolved value. The typedef won't always map to something.
  cpp_.emplace_back(
      "std::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >",
      std::make_unique<PrettyStdString>());
  cpp_.emplace_back("std::__2::string", std::make_unique<PrettyStdString>());

  // std::string_view
  cpp_.emplace_back("std::__2::basic_string_view<char, std::__2::char_traits<char> >",
                    std::make_unique<PrettyHeapString>("__data", "__size"));

  // std::vector
  //
  // Note that we don't have vector<bool> yet but need to add a pretty-printer for it to
  // preferentially match over the non-bool version (the longest match will be taken). This will
  // result in errors but it will be better than misleading results.
  cpp_.emplace_back("std::__2::vector<",
                    std::make_unique<PrettyArray>("__begin_", "__end_ - __begin_"));
  cpp_.emplace_back("std::__2::vector<bool, ",
                    std::make_unique<PrettyArray>("vector_bool_printer_not_implemented_yet",
                                                  "vector_bool_printer_not_implemented_yet"));
}

void PrettyTypeManager::AddDefaultRustPrettyTypes() {
  rust_.emplace_back("&str", std::make_unique<PrettyHeapString>("data_ptr", "length"));
  rust_.emplace_back("alloc::string::String",
                     std::make_unique<PrettyHeapString>("(char*)vec.buf.ptr.pointer", "vec.len"));
  rust_.emplace_back("alloc::vec::Vec<", std::make_unique<PrettyArray>("buf.ptr.pointer", "len"));

  // A BinaryHeap is a wrapper around a "Vec" named "data".
  rust_.emplace_back("alloc::collections::binary_heap::BinaryHeap<",
                     std::make_unique<PrettyArray>("data.buf.ptr.pointer", "data.len"));
}

}  // namespace zxdb
