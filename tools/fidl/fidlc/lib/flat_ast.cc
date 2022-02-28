// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include "fidl/flat/visitor.h"

namespace fidl::flat {

bool Element::IsDecl() const {
  switch (kind) {
    case Kind::kBits:
    case Kind::kBuiltin:
    case Kind::kConst:
    case Kind::kEnum:
    case Kind::kProtocol:
    case Kind::kResource:
    case Kind::kService:
    case Kind::kStruct:
    case Kind::kTable:
    case Kind::kTypeAlias:
    case Kind::kUnion:
      return true;
    case Kind::kLibrary:
    case Kind::kBitsMember:
    case Kind::kEnumMember:
    case Kind::kProtocolCompose:
    case Kind::kProtocolMethod:
    case Kind::kResourceProperty:
    case Kind::kServiceMember:
    case Kind::kStructMember:
    case Kind::kTableMember:
    case Kind::kUnionMember:
      return false;
  }
}

Decl* Element::AsDecl() {
  assert(IsDecl());
  return static_cast<Decl*>(this);
}

std::string Decl::GetName() const { return std::string(name.decl_name()); }

FieldShape Struct::Member::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Table::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Union::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

std::vector<std::reference_wrapper<const Union::Member>> Union::MembersSortedByXUnionOrdinal()
    const {
  std::vector<std::reference_wrapper<const Member>> sorted_members(members.cbegin(),
                                                                   members.cend());
  std::sort(sorted_members.begin(), sorted_members.end(),
            [](const auto& member1, const auto& member2) {
              return member1.get().ordinal->value < member2.get().ordinal->value;
            });
  return sorted_members;
}

Resource::Property* Resource::LookupProperty(std::string_view name) {
  for (Property& property : properties) {
    if (property.name.data() == name.data()) {
      return &property;
    }
  }
  return nullptr;
}

Dependencies::RegisterResult Dependencies::Register(
    const SourceSpan& span, std::string_view filename, Library* dep_library,
    const std::unique_ptr<raw::Identifier>& maybe_alias) {
  refs_.push_back(std::make_unique<LibraryRef>(span, dep_library));
  LibraryRef* ref = refs_.back().get();

  const std::vector<std::string_view> name =
      maybe_alias ? std::vector{maybe_alias->span().data()} : dep_library->name;
  auto iter = by_filename_.find(filename);
  if (iter == by_filename_.end()) {
    iter = by_filename_.emplace(filename, std::make_unique<PerFile>()).first;
  }
  PerFile& per_file = *iter->second;
  if (!per_file.libraries.insert(dep_library).second) {
    return RegisterResult::kDuplicate;
  }
  if (!per_file.refs.emplace(name, ref).second) {
    return RegisterResult::kCollision;
  }
  dependencies_aggregate_.insert(dep_library);
  return RegisterResult::kSuccess;
}

bool Dependencies::Contains(std::string_view filename, const std::vector<std::string_view>& name) {
  const auto iter = by_filename_.find(filename);
  if (iter == by_filename_.end()) {
    return false;
  }
  const PerFile& per_file = *iter->second;
  return per_file.refs.find(name) != per_file.refs.end();
}

Library* Dependencies::LookupAndMarkUsed(std::string_view filename,
                                         const std::vector<std::string_view>& name) const {
  auto iter1 = by_filename_.find(filename);
  if (iter1 == by_filename_.end()) {
    return nullptr;
  }

  auto iter2 = iter1->second->refs.find(name);
  if (iter2 == iter1->second->refs.end()) {
    return nullptr;
  }

  auto ref = iter2->second;
  ref->used = true;
  return ref->library;
}

void Dependencies::VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter) {
  for (const auto& [filename, per_file] : by_filename_) {
    for (const auto& [name, ref] : per_file->refs) {
      if (!ref->used) {
        reporter->Fail(ErrUnusedImport, ref->span, for_library.name, ref->library->name,
                       ref->library->name);
      }
    }
  }
}

std::string LibraryName(const Library* library, std::string_view separator) {
  if (library != nullptr) {
    return utils::StringJoin(library->name, separator);
  }
  return std::string();
}

// static
std::unique_ptr<Library> Library::CreateRootLibrary() {
  auto library = std::make_unique<Library>();
  library->name = {"fidl"};
  auto insert = [&](const char* name, Builtin::Kind kind) {
    auto decl = std::make_unique<Builtin>(kind, Name::CreateIntrinsic(library.get(), name));
    library->declarations.emplace(name, decl.get());
    library->builtin_declarations.push_back(std::move(decl));
  };
  insert("bool", Builtin::Kind::kBool);
  insert("int8", Builtin::Kind::kInt8);
  insert("int16", Builtin::Kind::kInt16);
  insert("int32", Builtin::Kind::kInt32);
  insert("int64", Builtin::Kind::kInt64);
  insert("uint8", Builtin::Kind::kUint8);
  insert("uint16", Builtin::Kind::kUint16);
  insert("uint32", Builtin::Kind::kUint32);
  insert("uint64", Builtin::Kind::kUint64);
  insert("float32", Builtin::Kind::kFloat32);
  insert("float64", Builtin::Kind::kFloat64);
  insert("string", Builtin::Kind::kString);
  insert("box", Builtin::Kind::kBox);
  insert("array", Builtin::Kind::kArray);
  insert("vector", Builtin::Kind::kVector);
  insert("client_end", Builtin::Kind::kClientEnd);
  insert("server_end", Builtin::Kind::kServerEnd);
  insert("byte", Builtin::Kind::kByte);
  insert("bytes", Builtin::Kind::kBytes);
  insert("optional", Builtin::Kind::kOptional);
  insert("MAX", Builtin::Kind::kMax);
  return library;
}

bool HasSimpleLayout(const Decl* decl) {
  return decl->attributes->Get("for_deprecated_c_bindings") != nullptr;
}

void Library::TraverseElements(const fit::function<void(Element*)>& fn) {
  fn(this);
  for (auto& [name, decl] : declarations) {
    fn(decl);
    decl->ForEachMember(fn);
  }
}

void Decl::ForEachMember(const fit::function<void(Element*)>& fn) {
  switch (kind) {
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kTypeAlias:
      break;
    case Decl::Kind::kBits:
      for (auto& member : static_cast<Bits*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kEnum:
      for (auto& member : static_cast<Enum*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kProtocol:
      for (auto& composed_protocol : static_cast<Protocol*>(this)->composed_protocols) {
        fn(&composed_protocol);
      }
      for (auto& method : static_cast<Protocol*>(this)->methods) {
        fn(&method);
      }
      break;
    case Decl::Kind::kResource:
      for (auto& member : static_cast<Resource*>(this)->properties) {
        fn(&member);
      }
      break;
    case Decl::Kind::kService:
      for (auto& member : static_cast<Service*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kStruct:
      for (auto& member : static_cast<Struct*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kTable:
      for (auto& member : static_cast<Table*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kUnion:
      for (auto& member : static_cast<Union*>(this)->members) {
        fn(&member);
      }
      break;
  }  // switch
}

TypeConstructor* LiteralLayoutParameter::AsTypeCtor() const { return nullptr; }
TypeConstructor* TypeLayoutParameter::AsTypeCtor() const { return type_ctor.get(); }
TypeConstructor* IdentifierLayoutParameter::AsTypeCtor() const { return as_type_ctor.get(); }
Constant* LiteralLayoutParameter::AsConstant() const { return literal.get(); }
Constant* TypeLayoutParameter::AsConstant() const { return nullptr; }
Constant* IdentifierLayoutParameter::AsConstant() const { return as_constant.get(); }

void IdentifierLayoutParameter::Disambiguate() {
  switch (reference.target()->kind) {
    case Element::Kind::kConst:
    case Element::Kind::kBitsMember:
    case Element::Kind::kEnumMember: {
      as_constant = std::make_unique<IdentifierConstant>(reference, span);
      break;
    }
    default: {
      as_type_ctor = std::make_unique<TypeConstructor>(
          reference, std::make_unique<LayoutParameterList>(), std::make_unique<TypeConstraints>());
      break;
    }
  }
}

std::any Bits::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Enum::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Protocol::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Service::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Struct::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Struct::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::Member::Used::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::Member::Used::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

}  // namespace fidl::flat
