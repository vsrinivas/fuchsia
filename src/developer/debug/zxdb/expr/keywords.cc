// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/keywords.h"

namespace zxdb {

namespace {

const char* kCKeywords[] = {
    "alignas",      "alignof",      "and",           "and_eq",
    "asm",          "auto",         "bitand",        "bitor",
    "bool",         "break",        "case",          "catch",
    "char",         "char8_t",      "char16_t",      "char32_t",
    "class",        "compl",        "concept",       "const",
    "consteval",    "constexpr",    "constinit",     "const_cast",
    "continue",     "co_await",     "co_return",     "co_yield",
    "decltype",     "default",      "delete",        "do",
    "double",       "dynamic_cast", "else",          "enum",
    "explicit",     "export",       "extern",        "false",
    "float",        "for",          "friend",        "goto",
    "if",           "inline",       "int",           "long",
    "mutable",      "namespace",    "new",           "noexcept",
    "not",          "not_eq",       "nullptr",       "operator",
    "or",           "or_eq",        "private",       "protected",
    "public",       "reflexpr",     "register",      "reinterpret_cast",
    "requires",     "return",       "short",         "signed",
    "sizeof",       "static",       "static_assert", "static_cast",
    "struct",       "switch",       "template",      "this",
    "thread_local", "throw",        "true",          "try",
    "typedef",      "typeid",       "typename",      "union",
    "unsigned",     "using",        "virtual",       "void",
    "volatile",     "wchar_t",      "while",         "xor",
    "xor_eq",
};

const std::set<std::string>& GetCKeywords() {
  static std::set<std::string> keywords;
  if (keywords.empty()) {
    for (const char* keyword : kCKeywords)
      keywords.insert(keyword);
  }
  return keywords;
}

// Extra "permissive" keywords for syntax highlighting purposes.
const char* kCKeywordsPermissive[] = {
    "int8_t",    "uint8_t",   "int16_t",     "uint16_t", "int32_t",   "uint32_t", "int64_t",
    "uint64_t",  "intptr_t",  "uintptr_t",   "intmax_t", "uintmax_t", "size_t",   "ssize_t",
    "ptrdiff_t", "nullptr_t", "max_align_t", "byte",     "offsetof",  "NULL",     "std",
};

const std::set<std::string>& GetCKeywordsPermissive() {
  static std::set<std::string> keywords;
  if (keywords.empty()) {
    for (const char* keyword : kCKeywords)
      keywords.insert(keyword);
    for (const char* keyword : kCKeywordsPermissive)
      keywords.insert(keyword);
  }
  return keywords;
}

const char* kRustKeywords[] = {
    "as",   "break",  "const",  "continue", "crate", "else",   "enum",   "extern", "false", "fn",
    "for",  "if",     "impl",   "in",       "let",   "loop",   "match",  "mod",    "move",  "mut",
    "pub",  "ref",    "return", "self",     "Self",  "static", "struct", "super",  "trait", "true",
    "type", "unsafe", "use",    "where",    "while", "async",  "await",  "dyn",    "union",
};

const std::set<std::string>& GetRustKeywords() {
  static std::set<std::string> keywords;
  if (keywords.empty()) {
    for (const char* keyword : kRustKeywords)
      keywords.insert(keyword);
  }
  return keywords;
}

// Extra "permissive" keywords for syntax highlighting purposes.
const char* kRustKeywordsPermissive[] = {
    "bool", "char", "i8",   "u8",    "i16",   "u16", "i32", "u32", "i64",
    "u64",  "i128", "u128", "isize", "usize", "f32", "f64", "str",
};

const std::set<std::string>& GetRustKeywordsPermissive() {
  static std::set<std::string> keywords;
  if (keywords.empty()) {
    for (const char* keyword : kRustKeywords)
      keywords.insert(keyword);
    for (const char* keyword : kRustKeywordsPermissive)
      keywords.insert(keyword);
  }
  return keywords;
}

}  // namespace

const std::set<std::string>& AllKeywordsForLanguage(ExprLanguage language, bool permissive) {
  switch (language) {
    case ExprLanguage::kC:
      if (permissive)
        return GetCKeywordsPermissive();
      return GetCKeywords();
    case ExprLanguage::kRust:
      if (permissive)
        return GetRustKeywordsPermissive();
      return GetRustKeywords();
  }

  static std::set<std::string> empty;
  return empty;
}

}  // namespace zxdb
