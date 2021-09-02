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

std::string Display(const raw::AttributeListOld* a) {
  std::stringstream attributes_found;
  for (auto it = a->attributes.begin(); it != a->attributes.end(); it++) {
    if (it != a->attributes.cbegin()) {
      attributes_found << ", ";
    }
    attributes_found << it->name;
  }
  return attributes_found.str();
}

std::string Display(const raw::AttributeListNew* a) {
  std::stringstream attributes_found;
  for (auto it = a->attributes.begin(); it != a->attributes.end(); it++) {
    if (it != a->attributes.cbegin()) {
      attributes_found << ", ";
    }
    attributes_found << (*it)->name;
  }
  return attributes_found.str();
}

std::string Display(const std::vector<std::string_view>& library_name) {
  return NameLibrary(library_name);
}

std::string Display(const flat::Attribute* a) { return a->name; }

std::string Display(const flat::AttributeArg* a) {
  return a->name.has_value() ? a->name.value() : "";
}

std::string Display(const flat::Constant* c) { return NameFlatConstant(c); }

std::string Display(const flat::Decl* d) {
  std::string decl_kind;
  switch (d->kind) {
    case flat::Decl::Kind::kBits: {
      decl_kind = "bits";
      break;
    }
    case flat::Decl::Kind::kConst: {
      decl_kind = "const";
      break;
    }
    case flat::Decl::Kind::kEnum: {
      decl_kind = "enum";
      break;
    }
    case flat::Decl::Kind::kProtocol: {
      decl_kind = "protocol";
      break;
    }
    case flat::Decl::Kind::kResource: {
      decl_kind = "resource";
      break;
    }
    case flat::Decl::Kind::kService: {
      decl_kind = "resource";
      break;
    }
    case flat::Decl::Kind::kStruct: {
      decl_kind = "struct";
      break;
    }
    case flat::Decl::Kind::kTable: {
      decl_kind = "table";
      break;
    }
    case flat::Decl::Kind::kTypeAlias: {
      decl_kind = "alias";
      break;
    }
    case flat::Decl::Kind::kUnion: {
      decl_kind = "union";
      break;
    }
  }

  if (d->name.is_sourced()) {
    return decl_kind + " " + d->GetName();
  }

  return decl_kind;
}

std::string Display(const flat::TypeConstructorOld* tc) { return NameFlatTypeConstructorOld(tc); }

std::string Display(const flat::Type* t) { return NameFlatType(t); }

std::string Display(const flat::TypeTemplate* t) { return NameFlatName(t->name()); }

std::string Display(const flat::Name& n) { return std::string(n.full_name()); }

}  // namespace internal
}  // namespace diagnostics
}  // namespace fidl
