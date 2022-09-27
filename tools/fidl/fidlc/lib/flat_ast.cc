// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat/visitor.h"
#include "tools/fidl/fidlc/include/fidl/reporter.h"
#include "tools/fidl/fidlc/include/fidl/virtual_source_file.h"

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
    case Kind::kNewType:
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
  ZX_ASSERT(IsDecl());
  return static_cast<Decl*>(this);
}

bool Element::IsAnonymousLayout() const {
  switch (kind) {
    case Element::Kind::kBits:
    case Element::Kind::kEnum:
    case Element::Kind::kStruct:
    case Element::Kind::kTable:
    case Element::Kind::kUnion:
      return static_cast<const Decl*>(this)->name.as_anonymous() != nullptr;
    default:
      return false;
  }
}

std::string Decl::GetName() const { return std::string(name.decl_name()); }

bool Builtin::IsInternal() const {
  switch (id) {
    case Identity::kBool:
    case Identity::kInt8:
    case Identity::kInt16:
    case Identity::kInt32:
    case Identity::kInt64:
    case Identity::kUint8:
    case Identity::kUint16:
    case Identity::kUint32:
    case Identity::kUint64:
    case Identity::kZxUsize:
    case Identity::kFloat32:
    case Identity::kFloat64:
    case Identity::kString:
    case Identity::kBox:
    case Identity::kArray:
    case Identity::kVector:
    case Identity::kClientEnd:
    case Identity::kServerEnd:
    case Identity::kByte:
    case Identity::kOptional:
    case Identity::kMax:
    case Identity::kHead:
      return false;
    case Identity::kTransportErr:
      return true;
  }
}

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

std::string LibraryName(const std::vector<std::string_view>& components,
                        std::string_view separator) {
  return utils::StringJoin(components, separator);
}

// static
std::unique_ptr<Library> Library::CreateRootLibrary() {
  // TODO(fxbug.dev/67858): Because this library doesn't get compiled, we have
  // to simulate what AvailabilityStep would do (set the platform, inherit the
  // availabilities). Perhaps we could make the root library less special and
  // compile it as well. That would require addressing circularity issues.
  auto library = std::make_unique<Library>();
  library->name = {"fidl"};
  library->platform = Platform::Parse("fidl").value();
  library->availability.Init(Version::Head(), std::nullopt, std::nullopt);
  library->availability.Inherit(Availability::Unbounded());
  auto insert = [&](const char* name, Builtin::Identity id) {
    auto decl = std::make_unique<Builtin>(id, Name::CreateIntrinsic(library.get(), name));
    decl->availability.Init(std::nullopt, std::nullopt, std::nullopt);
    decl->availability.Inherit(library->availability);
    library->declarations.Insert(std::move(decl));
  };
  // An assertion in Declarations::Insert ensures that these insertions
  // stays in sync with the order of Builtin::Identity.
  insert("bool", Builtin::Identity::kBool);
  insert("int8", Builtin::Identity::kInt8);
  insert("int16", Builtin::Identity::kInt16);
  insert("int32", Builtin::Identity::kInt32);
  insert("int64", Builtin::Identity::kInt64);
  insert("uint8", Builtin::Identity::kUint8);
  insert("uint16", Builtin::Identity::kUint16);
  insert("uint32", Builtin::Identity::kUint32);
  insert("uint64", Builtin::Identity::kUint64);
  insert("usize", Builtin::Identity::kZxUsize);
  insert("float32", Builtin::Identity::kFloat32);
  insert("float64", Builtin::Identity::kFloat64);
  insert("string", Builtin::Identity::kString);
  insert("box", Builtin::Identity::kBox);
  insert("array", Builtin::Identity::kArray);
  insert("vector", Builtin::Identity::kVector);
  insert("client_end", Builtin::Identity::kClientEnd);
  insert("server_end", Builtin::Identity::kServerEnd);
  insert("byte", Builtin::Identity::kByte);
  insert("TransportErr", Builtin::Identity::kTransportErr);
  insert("optional", Builtin::Identity::kOptional);
  insert("MAX", Builtin::Identity::kMax);
  insert("HEAD", Builtin::Identity::kHead);

  return library;
}

bool HasSimpleLayout(const Decl* decl) {
  return decl->attributes->Get("for_deprecated_c_bindings") != nullptr;
}

void Library::TraverseElements(const fit::function<void(Element*)>& fn) {
  fn(this);
  for (auto& [name, decl] : declarations.all) {
    fn(decl);
    decl->ForEachMember(fn);
  }
}

void Decl::ForEachMember(const fit::function<void(Element*)>& fn) {
  switch (kind) {
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kTypeAlias:
    case Decl::Kind::kNewType:
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

template <typename T>
static T* StoreDecl(std::unique_ptr<Decl> decl, std::multimap<std::string_view, Decl*>* all,
                    std::vector<std::unique_ptr<T>>* declarations) {
  auto ptr = static_cast<T*>(decl.release());
  all->emplace(ptr->name.decl_name(), ptr);
  declarations->emplace_back(ptr);
  return ptr;
}

Decl* Library::Declarations::Insert(std::unique_ptr<Decl> decl) {
  switch (decl->kind) {
    case Decl::Kind::kBuiltin: {
      auto index = static_cast<size_t>(static_cast<Builtin*>(decl.get())->id);
      ZX_ASSERT_MSG(index == builtins.size(), "inserted builtin out of order");
      return StoreDecl(std::move(decl), &all, &builtins);
    }
    case Decl::Kind::kBits:
      return StoreDecl(std::move(decl), &all, &bits);
    case Decl::Kind::kConst:
      return StoreDecl(std::move(decl), &all, &consts);
    case Decl::Kind::kEnum:
      return StoreDecl(std::move(decl), &all, &enums);
    case Decl::Kind::kProtocol:
      return StoreDecl(std::move(decl), &all, &protocols);
    case Decl::Kind::kResource:
      return StoreDecl(std::move(decl), &all, &resources);
    case Decl::Kind::kService:
      return StoreDecl(std::move(decl), &all, &services);
    case Decl::Kind::kStruct:
      return StoreDecl(std::move(decl), &all, &structs);
    case Decl::Kind::kTable:
      return StoreDecl(std::move(decl), &all, &tables);
    case Decl::Kind::kTypeAlias:
      return StoreDecl(std::move(decl), &all, &type_aliases);
    case Decl::Kind::kUnion:
      return StoreDecl(std::move(decl), &all, &unions);
    case Decl::Kind::kNewType:
      return StoreDecl(std::move(decl), &all, &new_types);
  }
}

Builtin* Library::Declarations::LookupBuiltin(Builtin::Identity id) const {
  auto index = static_cast<size_t>(id);
  ZX_ASSERT_MSG(index < builtins.size(), "builtin id out of range");
  auto builtin = builtins[index].get();
  ZX_ASSERT_MSG(builtin->id == id, "builtin's id does not match index");
  return builtin;
}

std::unique_ptr<TypeConstructor> TypeConstructor::Clone() const {
  return std::make_unique<TypeConstructor>(layout, parameters->Clone(), constraints->Clone());
}

std::unique_ptr<LayoutParameterList> LayoutParameterList::Clone() const {
  return std::make_unique<LayoutParameterList>(utils::MapClone(items), span);
}

std::unique_ptr<TypeConstraints> TypeConstraints::Clone() const {
  return std::make_unique<TypeConstraints>(utils::MapClone(items), span);
}

TypeConstructor* LiteralLayoutParameter::AsTypeCtor() const { return nullptr; }
TypeConstructor* TypeLayoutParameter::AsTypeCtor() const { return type_ctor.get(); }
TypeConstructor* IdentifierLayoutParameter::AsTypeCtor() const { return as_type_ctor.get(); }

Constant* LiteralLayoutParameter::AsConstant() const { return literal.get(); }
Constant* TypeLayoutParameter::AsConstant() const { return nullptr; }
Constant* IdentifierLayoutParameter::AsConstant() const { return as_constant.get(); }

std::unique_ptr<LayoutParameter> LiteralLayoutParameter::Clone() const {
  return std::make_unique<LiteralLayoutParameter>(literal->CloneLiteralConstant(), span);
}

std::unique_ptr<LayoutParameter> TypeLayoutParameter::Clone() const {
  return std::make_unique<TypeLayoutParameter>(type_ctor->Clone(), span);
}

std::unique_ptr<LayoutParameter> IdentifierLayoutParameter::Clone() const {
  ZX_ASSERT_MSG(!(as_constant || as_type_ctor), "Clone() is not allowed after Disambiguate()");
  return std::make_unique<IdentifierLayoutParameter>(reference, span);
}

void IdentifierLayoutParameter::Disambiguate() {
  switch (reference.resolved().element()->kind) {
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

std::unique_ptr<Decl> Decl::Split(VersionRange range) const {
  auto decl = SplitImpl(range);
  decl->availability = availability;
  decl->availability.Narrow(range);
  return decl;
}

// For a decl member type T that has a Copy() method, takes a vector<T> and
// returns a vector of copies filtered to only include those that intersect with
// the given range, and narrows their availabilities to that range.
template <typename T>
static std::vector<T> FilterMembers(const std::vector<T>& all, VersionRange range) {
  std::vector<T> result;
  for (auto& member : all) {
    if (VersionRange::Intersect(range, member.availability.range())) {
      result.push_back(member.Copy());
      result.back().availability = member.availability;
      result.back().availability.Narrow(range);
    }
  }
  return result;
}

std::unique_ptr<Decl> Builtin::SplitImpl(VersionRange range) const {
  ZX_PANIC("splitting builtins not allowed");
}

std::unique_ptr<Decl> Const::SplitImpl(VersionRange range) const {
  return std::make_unique<Const>(attributes->Clone(), name, type_ctor->Clone(), value->Clone());
}

std::unique_ptr<Decl> Enum::SplitImpl(VersionRange range) const {
  return std::make_unique<Enum>(attributes->Clone(), name, subtype_ctor->Clone(),
                                FilterMembers(members, range), strictness);
}

std::unique_ptr<Decl> Bits::SplitImpl(VersionRange range) const {
  return std::make_unique<Bits>(attributes->Clone(), name, subtype_ctor->Clone(),
                                FilterMembers(members, range), strictness);
}

std::unique_ptr<Decl> Service::SplitImpl(VersionRange range) const {
  return std::make_unique<Service>(attributes->Clone(), name, FilterMembers(members, range));
}

std::unique_ptr<Decl> Struct::SplitImpl(VersionRange range) const {
  return std::make_unique<Struct>(attributes->Clone(), name, FilterMembers(members, range),
                                  resourceness);
}

std::unique_ptr<Decl> Table::SplitImpl(VersionRange range) const {
  return std::make_unique<Table>(attributes->Clone(), name, FilterMembers(members, range),
                                 strictness, resourceness);
}

std::unique_ptr<Decl> Union::SplitImpl(VersionRange range) const {
  return std::make_unique<Union>(attributes->Clone(), name, FilterMembers(members, range),
                                 strictness, resourceness);
}

std::unique_ptr<Decl> Protocol::SplitImpl(VersionRange range) const {
  return std::make_unique<Protocol>(attributes->Clone(), openness, name,
                                    FilterMembers(composed_protocols, range),
                                    FilterMembers(methods, range));
}

std::unique_ptr<Decl> Resource::SplitImpl(VersionRange range) const {
  return std::make_unique<Resource>(attributes->Clone(), name, subtype_ctor->Clone(),
                                    FilterMembers(properties, range));
}

std::unique_ptr<Decl> TypeAlias::SplitImpl(VersionRange range) const {
  return std::make_unique<TypeAlias>(attributes->Clone(), name, partial_type_ctor->Clone());
}

std::unique_ptr<Decl> NewType::SplitImpl(VersionRange range) const {
  return std::make_unique<NewType>(attributes->Clone(), name, type_ctor->Clone());
}

Enum::Member Enum::Member::Copy() const {
  return Member(name, value->Clone(), attributes->Clone());
}

Bits::Member Bits::Member::Copy() const {
  return Member(name, value->Clone(), attributes->Clone());
}

Service::Member Service::Member::Copy() const {
  return Member(type_ctor->Clone(), name, attributes->Clone());
}

Struct::Member Struct::Member::Copy() const {
  return StructMember(type_ctor->Clone(), name,
                      maybe_default_value ? maybe_default_value->Clone() : nullptr,
                      attributes->Clone());
}

Table::Member Table::Member::Copy() const {
  return TableMember(ordinal, span, maybe_used ? maybe_used->Clone() : nullptr,
                     attributes->Clone());
}

Union::Member Union::Member::Copy() const {
  return UnionMember(ordinal, span, maybe_used ? maybe_used->Clone() : nullptr,
                     attributes->Clone());
}

Protocol::Method Protocol::Method::Copy() const {
  return Method(attributes->Clone(), strictness, identifier, name, has_request,
                maybe_request ? maybe_request->Clone() : nullptr, has_response,
                maybe_response ? maybe_response->Clone() : nullptr, has_error);
}

Protocol::ComposedProtocol Protocol::ComposedProtocol::Copy() const {
  return ComposedProtocol(attributes->Clone(), reference);
}

Resource::Property Resource::Property::Copy() const {
  return Property(type_ctor->Clone(), name, attributes->Clone());
}

std::any Bits::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Enum::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any NewType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
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
