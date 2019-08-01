// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include <limits>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/pretty_std_string.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// Used for internal hardcoded type globs, this parses the given identifier string and asserts if it
// can't be parsed. Since the built-in globs should always be parseable, this helps clean up the
// syntax.
TypeGlob InternalGlob(const char* glob) {
  TypeGlob result;
  Err err = result.Init(glob);
  FXL_CHECK(!err.has_error()) << "Internal pretty-printer parse failure for\" " << glob
                              << "\": " << err.msg();
  return result;
}

}  // namespace

PrettyTypeManager::PrettyTypeManager() {
  AddDefaultCppPrettyTypes();
  AddDefaultRustPrettyTypes();
}

PrettyTypeManager::~PrettyTypeManager() = default;

void PrettyTypeManager::Add(ExprLanguage lang, TypeGlob glob, std::unique_ptr<PrettyType> pretty) {
  switch (lang) {
    case ExprLanguage::kC:
      cpp_.emplace_back(std::move(glob), std::move(pretty));
      break;
    case ExprLanguage::kRust:
      rust_.emplace_back(std::move(glob), std::move(pretty));
      break;
  }
}

PrettyType* PrettyTypeManager::GetForType(const Type* type) const {
  if (!type)
    return nullptr;

  ParsedIdentifier type_ident = ToParsedIdentifier(type->GetIdentifier());

  // Pick the language-specific lookup.
  const auto* lookup = type->GetLanguage() == DwarfLang::kRust ? &rust_ : &cpp_;

  // Tracks the best one found so far. Lower scores are better.
  int best_score = std::numeric_limits<int>::max();
  PrettyType* best_type = nullptr;

  for (const auto& [glob, pretty_ptr] : *lookup) {
    if (auto match = glob.Matches(type_ident)) {
      if (*match < best_score) {
        // Got a new best match.
        best_score = *match;
        best_type = pretty_ptr.get();
      }
    }
  }

  return best_type;
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
      InternalGlob(
          "std::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >"),
      std::make_unique<PrettyStdString>());
  cpp_.emplace_back(InternalGlob("std::__2::string"), std::make_unique<PrettyStdString>());

  // std::string_view
  cpp_.emplace_back(
      InternalGlob("std::__2::basic_string_view<char, std::__2::char_traits<char> >"),
      new PrettyHeapString("__data", "__size", {{"size", "__size"}, {"length", "__size"}}));

  // std::vector
  //
  // Note that we don't have vector<bool> yet but need to add a pretty-printer for it to
  // preferentially match over the non-bool version (the longest match will be taken). This will
  // result in errors but it will be better than misleading results.
  cpp_.emplace_back(InternalGlob("std::__2::vector<*>"),
                    new PrettyArray("__begin_", "__end_ - __begin_",
                                    {{"size", "__end_ - __begin_"},
                                     {"capacity", "__end_cap_.__value_ - __begin_"}}));
  cpp_.emplace_back(InternalGlob("std::__2::vector<bool, *>"),
                    std::make_unique<PrettyArray>("vector_bool_printer_not_implemented_yet",
                                                  "vector_bool_printer_not_implemented_yet"));
}

void PrettyTypeManager::AddDefaultRustPrettyTypes() {
  // Rust's "&str" type won't parse as an identifier, construct an Identifier manually.
  rust_.emplace_back(TypeGlob(ParsedIdentifier(IdentifierQualification::kRelative,
                                               ParsedIdentifierComponent("&str"))),
                     std::make_unique<PrettyHeapString>("data_ptr", "length"));
  rust_.emplace_back(InternalGlob("alloc::string::String"),
                     std::make_unique<PrettyHeapString>("(char*)vec.buf.ptr.pointer", "vec.len"));
  rust_.emplace_back(InternalGlob("alloc::vec::Vec<*>"),
                     std::make_unique<PrettyArray>("buf.ptr.pointer", "len"));

  // A BinaryHeap is a wrapper around a "Vec" named "data".
  rust_.emplace_back(InternalGlob("alloc::collections::binary_heap::BinaryHeap<*>"),
                     std::make_unique<PrettyArray>("data.buf.ptr.pointer", "data.len"));
}

}  // namespace zxdb
