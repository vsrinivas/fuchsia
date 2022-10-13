// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/diagnostic_types.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat/typespace.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/names.h"
#include "tools/fidl/fidlc/include/fidl/raw_ast.h"
#include "tools/fidl/fidlc/include/fidl/source_span.h"

namespace fidl::internal {

std::string Display(const std::string& s) { return s; }

std::string Display(std::string_view s) { return std::string(s); }

// {'A', 'B', 'C'} -> "A, B, C"
std::string Display(const std::set<std::string>& s) {
  std::set<std::string_view> sv;
  for (const auto& str : s) {
    sv.insert(str);
  }
  return Display(sv);
}

std::string Display(const std::set<std::string_view>& s) {
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

std::string Display(const types::Openness o) {
  switch (o) {
    case types::Openness::kOpen:
      return "open";
    case types::Openness::kAjar:
      return "ajar";
    case types::Openness::kClosed:
      return "closed";
  }
}

std::string Display(const raw::AttributeList* a) {
  std::stringstream attributes_found;
  for (auto it = a->attributes.begin(); it != a->attributes.end(); it++) {
    if (it != a->attributes.cbegin()) {
      attributes_found << ", ";
    }
    const raw::Attribute* attribute = it->get();
    switch (attribute->provenance) {
      case raw::Attribute::Provenance::kDefault:
        attributes_found << (*it)->maybe_name->span().data();
        break;
      case raw::Attribute::Provenance::kDocComment:
        attributes_found << "(doc comment)";
        break;
    }
  }
  return attributes_found.str();
}

std::string Display(const std::vector<std::string_view>& library_name) {
  return NameLibrary(library_name);
}

std::string Display(const flat::Attribute* a) { return std::string(a->name.data()); }

std::string Display(const flat::AttributeArg* a) {
  return a->name.has_value() ? std::string(a->name.value().data()) : "";
}

std::string Display(const flat::Constant* c) { return NameFlatConstant(c); }

std::string Display(flat::Element::Kind k) {
  switch (k) {
    case flat::Element::Kind::kBits:
      return "bits";
    case flat::Element::Kind::kBitsMember:
      return "bits member";
    case flat::Element::Kind::kBuiltin:
      return "builtin";
    case flat::Element::Kind::kConst:
      return "const";
    case flat::Element::Kind::kEnum:
      return "enum";
    case flat::Element::Kind::kEnumMember:
      return "enum member";
    case flat::Element::Kind::kLibrary:
      return "library";
    case flat::Element::Kind::kNewType:
      return "new-type";
    case flat::Element::Kind::kProtocol:
      return "protocol";
    case flat::Element::Kind::kProtocolCompose:
      return "protocol composition";
    case flat::Element::Kind::kProtocolMethod:
      return "protocol method";
    case flat::Element::Kind::kResource:
      return "resource";
    case flat::Element::Kind::kResourceProperty:
      return "resource property";
    case flat::Element::Kind::kService:
      return "service";
    case flat::Element::Kind::kServiceMember:
      return "service member";
    case flat::Element::Kind::kStruct:
      return "struct";
    case flat::Element::Kind::kStructMember:
      return "struct member";
    case flat::Element::Kind::kTable:
      return "table";
    case flat::Element::Kind::kTableMember:
      return "table member";
    case flat::Element::Kind::kAlias:
      return "alias";
    case flat::Element::Kind::kUnion:
      return "union";
    case flat::Element::Kind::kUnionMember:
      return "union member";
  }
}

std::string Display(flat::Decl::Kind k) { return Display(flat::Decl::ElementKind(k)); }

std::string Display(const flat::Element* e) {
  std::stringstream ss;

  switch (e->kind) {
    case flat::Element::Kind::kTableMember: {
      auto table_member = static_cast<const flat::Table::Member*>(e);
      if (!table_member->maybe_used) {
        ss << "reserved " << Display(e->kind);
        return ss.str();
      }
      break;
    }
    case flat::Element::Kind::kUnionMember: {
      auto table_member = static_cast<const flat::Union::Member*>(e);
      if (!table_member->maybe_used) {
        ss << "reserved " << Display(e->kind);
        return ss.str();
      }
      break;
    }
    default:
      break;
  }

  ss << Display(e->kind) << " '";

  switch (e->kind) {
    case flat::Element::Kind::kBits:
    case flat::Element::Kind::kBuiltin:
    case flat::Element::Kind::kConst:
    case flat::Element::Kind::kEnum:
    case flat::Element::Kind::kNewType:
    case flat::Element::Kind::kProtocol:
    case flat::Element::Kind::kResource:
    case flat::Element::Kind::kService:
    case flat::Element::Kind::kStruct:
    case flat::Element::Kind::kTable:
    case flat::Element::Kind::kAlias:
    case flat::Element::Kind::kUnion:
      ss << static_cast<const flat::Decl*>(e)->name.decl_name();
      break;
    case flat::Element::Kind::kBitsMember:
      ss << static_cast<const flat::Bits::Member*>(e)->name.data();
      break;
    case flat::Element::Kind::kEnumMember:
      ss << static_cast<const flat::Enum::Member*>(e)->name.data();
      break;
    case flat::Element::Kind::kLibrary:
      ss << Display(static_cast<const flat::Library*>(e)->name);
      break;
    case flat::Element::Kind::kProtocolCompose:
      ss << Display(
          static_cast<const flat::Protocol::ComposedProtocol*>(e)->reference.span().data());
      break;
    case flat::Element::Kind::kProtocolMethod:
      ss << static_cast<const flat::Protocol::Method*>(e)->name.data();
      break;
    case flat::Element::Kind::kResourceProperty:
      ss << static_cast<const flat::Resource::Property*>(e)->name.data();
      break;
    case flat::Element::Kind::kServiceMember:
      ss << static_cast<const flat::Service::Member*>(e)->name.data();
      break;
    case flat::Element::Kind::kStructMember:
      ss << static_cast<const flat::Struct::Member*>(e)->name.data();
      break;
    case flat::Element::Kind::kTableMember: {
      auto table_member = static_cast<const flat::Table::Member*>(e);
      if (auto& used = table_member->maybe_used) {
        ss << used->name.data();
      }
      break;
    }
    case flat::Element::Kind::kUnionMember: {
      auto union_member = static_cast<const flat::Union::Member*>(e);
      if (auto& used = union_member->maybe_used) {
        ss << used->name.data();
      }
      break;
    }
  }

  ss << "'";
  return ss.str();
}

// Display a list of nested types with arrows indicating what includes what:
// ['A', 'B', 'C'] -> "A -> B -> C"
std::string Display(std::vector<const flat::Decl*>& d) {
  std::stringstream ss;
  for (auto it = d.cbegin(); it != d.cend(); it++) {
    if (it != d.cbegin()) {
      ss << " -> ";
    }
    ss << Display(*it);
  }
  return ss.str();
}

std::string Display(const flat::Type* t) { return NameFlatType(t); }

std::string Display(const flat::Name& n) { return std::string(n.full_name()); }

std::string Display(const Platform& p) { return p.name(); }

std::string Display(const Version& v) { return v.ToString(); }

std::string Display(const VersionRange& r) {
  // Here we assume the version range is for an error about a versioned element.
  // We handle 4 special cases (-inf, +inf, HEAD, LEGACY) for each endpoint.
  auto [a, b] = r.pair();
  std::stringstream ss;
  if (a == Version::NegInf()) {
    ZX_PANIC("versioned elements cannot start at -inf");
  } else if (a == Version::PosInf()) {
    ZX_PANIC("versioned elements cannot start at +inf");
  } else if (a == Version::Head() || a == Version::Legacy()) {
    ZX_ASSERT_MSG(b == Version::PosInf(), "unexpected end version");
    // Technically [HEAD, +inf) includes LEGACY, but we just say "at version
    // HEAD" because this will show up in contexts where mentioning LEGACY would
    // be confusing (e.g. when the `legacy` argument is not used at all).
    ss << "at version " << Display(a);
  } else {
    if (b == Version::NegInf()) {
      ZX_PANIC("versioned elements cannot end at -inf");
    } else if (b == Version::PosInf()) {
      ss << "from version " << Display(a) << " onward";
    } else if (b == Version::Head()) {
      ss << "from version " << Display(a) << " to " << Display(b);
    } else if (b == Version::Legacy()) {
      ZX_PANIC("versioned elements cannot end at LEGACY");
    } else if (a.ordinal() + 1 == b.ordinal()) {
      ss << "at version " << Display(a);
    } else {
      ss << "from version " << Display(a) << " to "
         << Display(Version::From(b.ordinal() - 1).value());
    }
  }
  return ss.str();
}

std::string Display(const VersionSet& s) {
  auto& [x, maybe_y] = s.ranges();
  if (!maybe_y) {
    return Display(x);
  }
  ZX_ASSERT_MSG(x.pair().second != Version::PosInf(),
                "first range must have finite end if there are two");
  return Display(x) + " and " + Display(maybe_y.value());
}

}  // namespace fidl::internal
