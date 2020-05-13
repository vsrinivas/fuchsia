// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/diagnostic_types.h"

#include "fidl/flat_ast.h"
#include "fidl/names.h"
#include "fidl/raw_ast.h"
#include "fidl/source_span.h"

namespace fidl {
namespace diagnostics {
namespace internal {

std::string Display(const std::string& s) { return s; }

std::string Display(std::string_view s) { return std::string(s); }

// {'A', 'B', 'C'} -> "A, B, C"
std::string Display(const std::set<std::string>& s) {
  std::stringstream ss;
  for (auto it = s.begin(); it != s.end(); it++) {
    if (it != s.cbegin()) {
      ss << ", ";
    }
    ss << *it;
  }
  return ss.str();
}

std::string Display(const SourceSpan& s) { return s.position_str(); }

std::string Display(const Token::KindAndSubkind& t) { return std::string(Token::Name(t)); }

std::string Display(const raw::Attribute& a) { return a.name; }

std::string Display(const raw::AttributeList& a) {
  std::stringstream attributes_found;
  for (auto it = a.attributes.begin(); it != a.attributes.end(); it++) {
    if (it != a.attributes.cbegin()) {
      attributes_found << ", ";
    }
    attributes_found << it->name;
  }
  return attributes_found.str();
}

std::string Display(const std::vector<std::string_view>& library_name) {
  return NameLibrary(library_name);
}

std::string Display(const flat::Constant* c) { return NameFlatConstant(c); }

std::string Display(const flat::TypeConstructor* tc) { return NameFlatTypeConstructor(tc); }

std::string Display(const flat::Type* t) { return NameFlatType(t); }

std::string Display(const flat::TypeTemplate* t) { return NameFlatName(t->name()); }

std::string Display(const flat::Name& n) { return std::string(n.full_name()); }

}  // namespace internal
}  // namespace diagnostics
}  // namespace fidl
