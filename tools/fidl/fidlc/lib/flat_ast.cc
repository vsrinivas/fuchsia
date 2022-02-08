// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include "fidl/flat/attribute_schema.h"
#include "fidl/flat/compile_step.h"
#include "fidl/flat/consume_step.h"
#include "fidl/flat/sort_step.h"
#include "fidl/flat/verify_steps.h"
#include "fidl/flat/visitor.h"

namespace fidl::flat {

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

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

Decl* Library::LookupDeclByName(Name::Key name) const {
  auto iter = declarations.find(name);
  if (iter == declarations.end()) {
    return nullptr;
  }
  return iter->second;
}

bool HasSimpleLayout(const Decl* decl) {
  return decl->attributes->Get("for_deprecated_c_bindings") != nullptr;
}

std::set<const Library*, LibraryComparator> Library::DirectAndComposedDependencies() const {
  std::set<const Library*, LibraryComparator> direct_dependencies;
  auto add_constant_deps = [&](const Constant* constant) {
    if (constant->kind != Constant::Kind::kIdentifier)
      return;
    auto* dep_library = static_cast<const IdentifierConstant*>(constant)->name.library();
    assert(dep_library != nullptr && "all identifier constants have a library");
    direct_dependencies.insert(dep_library);
  };
  auto add_type_ctor_deps = [&](const TypeConstructor& type_ctor) {
    if (auto dep_library = type_ctor.name.library())
      direct_dependencies.insert(dep_library);

    // TODO(fxbug.dev/64629): Add dependencies introduced through handle constraints.
    // This code currently assumes the handle constraints are always defined in the same
    // library as the resource_definition and so does not check for them separately.
    const auto& invocation = type_ctor.resolved_params;
    if (invocation.size_raw)
      add_constant_deps(invocation.size_raw);
    if (invocation.protocol_decl_raw)
      add_constant_deps(invocation.protocol_decl_raw);
    if (invocation.element_type_raw != nullptr) {
      if (auto dep_library = invocation.element_type_raw->name.library())
        direct_dependencies.insert(dep_library);
    }
    if (invocation.boxed_type_raw != nullptr) {
      if (auto dep_library = invocation.boxed_type_raw->name.library())
        direct_dependencies.insert(dep_library);
    }
  };
  for (const auto& dep_library : dependencies.all()) {
    direct_dependencies.insert(dep_library);
  }
  // Discover additional dependencies that are required to support
  // cross-library protocol composition.
  for (const auto& protocol : protocol_declarations) {
    for (const auto method_with_info : protocol->all_methods) {
      if (method_with_info.method->maybe_request) {
        auto id =
            static_cast<const flat::IdentifierType*>(method_with_info.method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        for (const auto& member : as_struct->members) {
          add_type_ctor_deps(*member.type_ctor);
        }
      }
      if (method_with_info.method->maybe_response) {
        auto id =
            static_cast<const flat::IdentifierType*>(method_with_info.method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        for (const auto& member : as_struct->members) {
          add_type_ctor_deps(*member.type_ctor);
        }
      }
      direct_dependencies.insert(method_with_info.method->owning_protocol->name.library());
    }
  }
  direct_dependencies.erase(this);
  return direct_dependencies;
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

std::unique_ptr<TypeConstructor> TypeConstructor::CreateSizeType() {
  return std::make_unique<TypeConstructor>(
      Name::CreateIntrinsic("uint32"), std::make_unique<LayoutParameterList>(),
      std::make_unique<TypeConstraints>(), /*span=*/std::nullopt);
}

Constant* LiteralLayoutParameter::AsConstant() const { return literal.get(); }
Constant* TypeLayoutParameter::AsConstant() const { return nullptr; }
Constant* IdentifierLayoutParameter::AsConstant() const {
  if (!as_constant) {
    as_constant = std::make_unique<IdentifierConstant>(name, span);
  }
  return as_constant.get();
}

TypeConstructor* LiteralLayoutParameter::AsTypeCtor() const { return nullptr; }
TypeConstructor* TypeLayoutParameter::AsTypeCtor() const { return type_ctor.get(); }
TypeConstructor* IdentifierLayoutParameter::AsTypeCtor() const {
  if (!as_type_ctor) {
    as_type_ctor = std::make_unique<TypeConstructor>(name, std::make_unique<LayoutParameterList>(),
                                                     std::make_unique<TypeConstraints>(),
                                                     /*span=*/std::nullopt);
  }

  return as_type_ctor.get();
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
