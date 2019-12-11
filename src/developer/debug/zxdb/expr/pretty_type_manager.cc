// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include <limits>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/pretty_rust_tuple.h"
#include "src/developer/debug/zxdb/expr/pretty_std_string.h"
#include "src/developer/debug/zxdb/expr/pretty_tree.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

using GetterList = std::initializer_list<std::pair<std::string, std::string>>;

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
  AddDefaultFuchsiaCppPrettyTypes();
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

PrettyType* PrettyTypeManager::GetForType(const Type* in_type) const {
  if (!in_type)
    return nullptr;

  // Strip const-volatile qualifiers for the name comparison, but don't follow typedefs or make
  // the type concrete. Typedefs will change the name and some pretty-printers are defined for
  // typedefs of other values. We need to maintain the original name for this comparison.
  const Type* type = in_type->StripCV();
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

  if (best_type) {
    return best_type;
  }

  if (type->GetLanguage() == DwarfLang::kRust) {
    const Collection* coll = type->AsCollection();
    if (!coll) {
      return nullptr;
    }

    auto special = coll->GetSpecialType();
    if (special == Collection::kRustTuple || special == Collection::kRustTupleStruct) {
      return rust_tuple_type_.get();
    }
  }

  return nullptr;
}

bool PrettyTypeManager::Format(FormatNode* node, const Type* type, const FormatOptions& options,
                               const fxl::RefPtr<EvalContext>& context,
                               fit::deferred_callback& cb) const {
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
          "std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >"),
      std::make_unique<PrettyStdString>());
  cpp_.emplace_back(InternalGlob("std::__2::string"), std::make_unique<PrettyStdString>());

  // std::string_view
  cpp_.emplace_back(InternalGlob("std::__2::basic_string_view<char, std::__2::char_traits<char> >"),
                    std::make_unique<PrettyHeapString>("__data", "__size",
                                                       GetterList{{"back", "__data[__size - 1]"},
                                                                  {"data", "__data"},
                                                                  {"front", "*__data"},
                                                                  {"size", "__size"},
                                                                  {"length", "__size"},
                                                                  {"empty", "__size == 0"}}));

  // std::vector
  //
  // Note that we don't have vector<bool> yet but need to add a pretty-printer for it to
  // preferentially match over the non-bool version (the longest match will be taken). This will
  // result in errors but it will be better than misleading results.
  cpp_.emplace_back(
      InternalGlob("std::__2::vector<*>"),
      std::make_unique<PrettyArray>("__begin_", "__end_ - __begin_",
                                    GetterList{{"size", "__end_ - __begin_"},
                                               {"capacity", "__end_cap_.__value_ - __begin_"},
                                               {"data", "__begin_"},
                                               {"empty", "__end_ == __begin_"},
                                               {"front", "*__begin_"},
                                               {"back", "__begin_[__end_ - __begin_ - 1]"}}));
  cpp_.emplace_back(InternalGlob("std::__2::vector<bool, *>"),
                    std::make_unique<PrettyArray>("vector_bool_printer_not_implemented_yet",
                                                  "vector_bool_printer_not_implemented_yet"));

  // Smart pointers.
  cpp_.emplace_back(InternalGlob("std::__2::unique_ptr<*>"),
                    std::make_unique<PrettyPointer>("__ptr_.__value_"));
  cpp_.emplace_back(InternalGlob("std::__2::shared_ptr<*>"),
                    std::make_unique<PrettyPointer>("__ptr_"));
  cpp_.emplace_back(InternalGlob("std::__2::weak_ptr<*>"),
                    std::make_unique<PrettyPointer>("__ptr_"));

  cpp_.emplace_back(InternalGlob("std::__2::optional<*>"),
                    std::make_unique<PrettyOptional>(
                        "std::optional", "__engaged_", "__val_", "std::nullopt",
                        GetterList{{"value", "__val_"}, {"has_value", "__engaged_"}}));

  cpp_.emplace_back(
      InternalGlob("std::__2::variant<*>"),
      std::make_unique<PrettyRecursiveVariant>(
          "std::variant", "__impl.__data", "__impl.__index", "__tail", "__head.__value",
          "std::variant::valueless_by_exception()", GetterList({{"index", "__impl.__index"}})));

  // Trees (std::set and std::map).
  cpp_.emplace_back(InternalGlob("std::__2::set<*>"), std::make_unique<PrettyTree>("std::set"));
  cpp_.emplace_back(InternalGlob("std::__2::map<*>"), std::make_unique<PrettyTree>("std::map"));
  cpp_.emplace_back(InternalGlob("std::__2::__tree_iterator<*>"),
                    std::make_unique<PrettyTreeIterator>());
  cpp_.emplace_back(InternalGlob("std::__2::__tree_const_iterator<*>"),
                    std::make_unique<PrettyTreeIterator>());
  cpp_.emplace_back(InternalGlob("std::__2::__map_iterator<*>"),
                    std::make_unique<PrettyMapIterator>());
  cpp_.emplace_back(InternalGlob("std::__2::__map_const_iterator<*>"),
                    std::make_unique<PrettyMapIterator>());
}

void PrettyTypeManager::AddDefaultRustPrettyTypes() {
  rust_tuple_type_ = std::make_unique<PrettyRustTuple>();

  // Rust's "&str" type won't parse as an identifier, construct an Identifier manually.
  rust_.emplace_back(TypeGlob(ParsedIdentifier(IdentifierQualification::kRelative,
                                               ParsedIdentifierComponent("&str"))),
                     std::make_unique<PrettyHeapString>("data_ptr", "length",
                                                        GetterList{{"as_ptr", "data_ptr"},
                                                                   {"as_mut_ptr", "data_ptr"},
                                                                   {"len", "length"},
                                                                   {"is_empty", "length == 0"}}));
  rust_.emplace_back(
      InternalGlob("alloc::string::String"),
      std::make_unique<PrettyHeapString>("vec.buf.ptr.pointer as *u8", "vec.len",
                                         GetterList{{"as_ptr", "vec.buf.ptr.pointer as *u8"},
                                                    {"as_mut_ptr", "vec.buf.ptr.pointer as *u8"},
                                                    {"len", "vec.len"},
                                                    {"capacity", "vec.buf.cap"},
                                                    {"is_empty", "vec.len == 0"}}));
  rust_.emplace_back(InternalGlob("alloc::vec::Vec<*>"),
                     std::make_unique<PrettyArray>("buf.ptr.pointer", "len",
                                                   GetterList{{"as_ptr", "buf.ptr.pointer"},
                                                              {"as_mut_ptr", "buf.ptr.pointer"},
                                                              {"len", "len"},
                                                              {"capacity", "buf.cap"},
                                                              {"is_empty", "len == 0"}}));

  // A BinaryHeap is a wrapper around a "Vec" named "data".
  rust_.emplace_back(InternalGlob("alloc::collections::binary_heap::BinaryHeap<*>"),
                     std::make_unique<PrettyArray>("data.buf.ptr.pointer", "data.len",
                                                   GetterList{{"len", "data.len"},
                                                              {"capacity", "data.buf.cap"},
                                                              {"is_empty", "data.len == 0"}}));

  // Smart pointers.
  rust_.emplace_back(
      InternalGlob("alloc::sync::Arc<*>"),
      std::make_unique<PrettyPointer>("ptr.pointer",
                                      GetterList{{"weak_count", "ptr.pointer->weak.v.value"},
                                                 {"strong_count", "ptr.pointer->strong.v.value"}}));
  rust_.emplace_back(
      InternalGlob("core::ptr::non_null::NonNull<*>"),
      std::make_unique<PrettyPointer>("pointer", GetterList{{"as_ptr", "ptr.pointer"},
                                                            {"as_ref", "*ptr.pointer"},
                                                            {"as_mut", "*ptr.pointer"}}));

  // Rust's wrapper for zx_status_t
  rust_.emplace_back(InternalGlob("fuchsia_zircon_status::Status"),
                     std::make_unique<PrettyRustZirconStatus>());
}

void PrettyTypeManager::AddDefaultFuchsiaCppPrettyTypes() {
  // Zircon.
  cpp_.emplace_back(InternalGlob("zx_status_t"), std::make_unique<PrettyZxStatusT>());

// fbl
#define FBL_STRING_LENGTH_EXPRESSION \
  "*reinterpret_cast<size_t*>(data_ - kDataFieldOffset + kLengthFieldOffset)"
  cpp_.emplace_back(
      InternalGlob("fbl::String"),
      std::make_unique<PrettyHeapString>("data_", FBL_STRING_LENGTH_EXPRESSION,
                                         GetterList{{"data", "data_"},
                                                    {"c_str", "data_"},
                                                    {"length", FBL_STRING_LENGTH_EXPRESSION},
                                                    {"size", FBL_STRING_LENGTH_EXPRESSION},
                                                    {"empty", "!" FBL_STRING_LENGTH_EXPRESSION}}));
  cpp_.emplace_back(InternalGlob("fbl::Span<*>"),
                    std::make_unique<PrettyArray>(
                        "ptr_", "size_",
                        GetterList{{"size", "size_"}, {"data", "ptr_"}, {"empty", "size_ == 0"}}));
  cpp_.emplace_back(InternalGlob("fbl::Vector<*>"),
                    std::make_unique<PrettyArray>("ptr_", "size_",
                                                  GetterList{{"size", "size_"},
                                                             {"get", "ptr_"},
                                                             {"capacity", "capacity_"},
                                                             {"is_empty", "size_ == 0"}}));
  cpp_.emplace_back(InternalGlob("fbl::RefPtr<*>"),
                    std::make_unique<PrettyPointer>("ptr_", GetterList{{"get", "ptr_"}}));
  cpp_.emplace_back(
      InternalGlob("fbl::RefCounted<*>"),
      std::make_unique<PrettyStruct>(GetterList{{"ref_count_", "ref_count_.__a_.__a_value"}}));

  // fit
  cpp_.emplace_back(
      InternalGlob("fit::optional<*>"),
      std::make_unique<PrettyOptional>(
          "fit::optional", "storage_.index_ == 0", "storage_.base_.value", "fit::nullopt",
          GetterList{{"value", "storage_.base_.value"}, {"has_value", "storage_.index_ == 0"}}));
  cpp_.emplace_back(InternalGlob("fit::variant<*>"),
                    std::make_unique<PrettyRecursiveVariant>(
                        "fit::variant", "storage_.base_", "storage_.index_", "rest", "value",
                        "fit::variant::empty", GetterList({{"index", "storage_.index_"}})));

  // fxl
  cpp_.emplace_back(InternalGlob("fxl::RefPtr<*>"),
                    std::make_unique<PrettyPointer>("ptr_", GetterList{{"get", "ptr_"}}));
  cpp_.emplace_back(
      InternalGlob("fxl::RefCountedThreadSafe<*>"),
      std::make_unique<PrettyStruct>(GetterList{{"ref_count_", "ref_count_.__a_.__a_value"}}));
}

}  // namespace zxdb
