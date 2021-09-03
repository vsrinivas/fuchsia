// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the implementations of the Accept methods for the AST
// nodes.  Generally, all they do is invoke the appropriate TreeVisitor method
// for each field of the node.

#include "fidl/raw_ast.h"

#include <map>

#include "fidl/tree_visitor.h"

namespace fidl {
namespace raw {

bool IsAttributeListDefined(const raw::AttributeList& attributes) {
  return std::visit(
      fidl::utils::matchers{
          [](const std::unique_ptr<raw::AttributeListOld>& e) -> bool { return e != nullptr; },
          [](const std::unique_ptr<raw::AttributeListNew>& e) -> bool { return e != nullptr; },
      },
      attributes);
}

bool IsAttributeListNotEmpty(const raw::AttributeList& attributes) {
  return std::visit(fidl::utils::matchers{
                        [](const std::unique_ptr<raw::AttributeListOld>& e) -> bool {
                          return e && !e->attributes.empty();
                        },
                        [](const std::unique_ptr<raw::AttributeListNew>& e) -> bool {
                          return e && !e->attributes.empty();
                        },
                    },
                    attributes);
}

bool IsTypeConstructorDefined(const raw::TypeConstructor& maybe_type_ctor) {
  return std::visit(
      fidl::utils::matchers{
          [](const std::unique_ptr<raw::TypeConstructorOld>& e) -> bool { return e != nullptr; },
          [](const std::unique_ptr<raw::TypeConstructorNew>& e) -> bool { return e != nullptr; },
      },
      maybe_type_ctor);
}

bool IsParameterListDefined(const raw::ParameterList& maybe_parameter_list) {
  return std::visit([](const auto& e) -> bool { return e != nullptr; }, maybe_parameter_list);
}

SourceSpan GetSpan(const raw::ParameterList& parameter_list) {
  return std::visit([](const auto& e) -> SourceSpan { return e->span(); }, parameter_list);
}

SourceElementMark::SourceElementMark(TreeVisitor* tv, const SourceElement& element)
    : tv_(tv), element_(element) {
  tv_->OnSourceElementStart(element_);
}

SourceElementMark::~SourceElementMark() { tv_->OnSourceElementEnd(element_); }

void Identifier::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void CompoundIdentifier::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : components) {
    visitor->OnIdentifier(i);
  }
}

void DocCommentLiteral::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
}

void StringLiteral::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void NumericLiteral::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void TrueLiteral::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void FalseLiteral::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void IdentifierConstant::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnCompoundIdentifier(identifier);
}

void LiteralConstant::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnLiteral(literal);
}

void BinaryOperatorConstant::Accept(TreeVisitor* visitor) const {
  // TODO(fxbug.dev/43758): Visit the operator as well.
  SourceElementMark sem(visitor, *this);
  visitor->OnConstant(left_operand);
  visitor->OnConstant(right_operand);
}

void Ordinal64::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void AttributeArg::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (value) {
    visitor->OnConstant(value);
  }
}

void AttributeOld::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (value) {
    visitor->OnLiteral(value);
  }
}

void AttributeNew::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : args) {
    visitor->OnAttributeArg(i);
  }
}

void AttributeListOld::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : attributes) {
    visitor->OnAttributeOld(i);
  }
}

void AttributeListNew::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : attributes) {
    visitor->OnAttributeNew(i);
  }
}

void TypeConstructorOld::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnCompoundIdentifier(identifier);
  if (maybe_arg_type_ctor != nullptr)
    visitor->OnTypeConstructorOld(maybe_arg_type_ctor);
  if (handle_subtype_identifier)
    visitor->OnIdentifier(handle_subtype_identifier);
  if (handle_rights != nullptr)
    visitor->OnConstant(handle_rights);
  if (maybe_size != nullptr)
    visitor->OnConstant(maybe_size);
  visitor->OnNullability(nullability);
}

void LibraryDecl::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(path);
}

void Using::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(using_path);
  if (maybe_alias != nullptr) {
    visitor->OnIdentifier(maybe_alias);
  }
}

void AliasDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(alias);
  visitor->OnTypeConstructor(type_ctor);
}

void BitsMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  visitor->OnConstant(value);
}

void BitsDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (maybe_type_ctor != nullptr) {
    visitor->OnTypeConstructorOld(maybe_type_ctor);
  }
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnBitsMember(*member);
  }
}

void ConstDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnTypeConstructor(type_ctor);
  visitor->OnIdentifier(identifier);
  visitor->OnConstant(constant);
}

void EnumMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  visitor->OnConstant(value);
}

void EnumDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (maybe_type_ctor != nullptr) {
    visitor->OnTypeConstructorOld(maybe_type_ctor);
  }
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnEnumMember(*member);
  }
}

void Parameter::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnTypeConstructor(type_ctor);
  visitor->OnIdentifier(identifier);
}

void ParameterListOld::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto parameter = parameter_list.begin(); parameter != parameter_list.end(); ++parameter) {
    visitor->OnParameter(*parameter);
  }
}

void ParameterListNew::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (type_ctor) {
    visitor->OnTypeConstructorNew(type_ctor);
  }
}

void ProtocolMethod::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (raw::IsParameterListDefined(maybe_request)) {
    visitor->OnParameterList(maybe_request);
  }
  if (raw::IsParameterListDefined(maybe_response)) {
    visitor->OnParameterList(maybe_response);
  }
  if (raw::IsTypeConstructorDefined(maybe_error_ctor)) {
    visitor->OnTypeConstructor(maybe_error_ctor);
  }
}

void ProtocolCompose::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(protocol_name);
}

void ProtocolDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto composed_protocol = composed_protocols.begin();
       composed_protocol != composed_protocols.end(); ++composed_protocol) {
    visitor->OnProtocolCompose(*composed_protocol);
  }
  for (auto method = methods.begin(); method != methods.end(); ++method) {
    visitor->OnProtocolMethod(*method);
  }
}

void ResourceProperty::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnTypeConstructor(type_ctor);
  visitor->OnIdentifier(identifier);
}

void ResourceDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (IsTypeConstructorDefined(maybe_type_ctor)) {
    visitor->OnTypeConstructor(maybe_type_ctor);
  }
  for (auto property = properties.begin(); property != properties.end(); ++property) {
    visitor->OnResourceProperty(*property);
  }
}

void ServiceMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnTypeConstructor(type_ctor);
  visitor->OnIdentifier(identifier);
}

void ServiceDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (IsAttributeListDefined(attributes)) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnServiceMember(*member);
  }
}

void StructMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnTypeConstructorOld(type_ctor);
  visitor->OnIdentifier(identifier);
  if (maybe_default_value != nullptr) {
    visitor->OnConstant(maybe_default_value);
  }
}

void StructDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnStructMember(*member);
  }
}

void TableMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (maybe_used != nullptr) {
    if (maybe_used->attributes != nullptr) {
      visitor->OnAttributeListOld(maybe_used->attributes);
    }
  }
  visitor->OnOrdinal64(*ordinal);
  if (maybe_used != nullptr) {
    visitor->OnTypeConstructorOld(maybe_used->type_ctor);
    visitor->OnIdentifier(maybe_used->identifier);
    if (maybe_used->maybe_default_value != nullptr) {
      visitor->OnConstant(maybe_used->maybe_default_value);
    }
  }
}

void TableDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnTableMember(*member);
  }
}

void UnionMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (maybe_used != nullptr) {
    if (maybe_used->attributes != nullptr) {
      visitor->OnAttributeListOld(maybe_used->attributes);
    }
  }
  visitor->OnOrdinal64(*ordinal);
  if (maybe_used != nullptr) {
    visitor->OnTypeConstructorOld(maybe_used->type_ctor);
    visitor->OnIdentifier(maybe_used->identifier);
    if (maybe_used->maybe_default_value != nullptr) {
      visitor->OnConstant(maybe_used->maybe_default_value);
    }
  }
}

void UnionDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListOld(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnUnionMember(*member);
  }
}

// TODO(fxbug.dev/70247): Remove these guards and old syntax visitors.
// --- start new syntax ---
void Modifiers::Accept(TreeVisitor* visitor) const { SourceElementMark sem(visitor, *this); }

void IdentifierLayoutParameter::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnCompoundIdentifier(identifier);
}

void LiteralLayoutParameter::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnLiteralConstant(literal);
}

void TypeLayoutParameter::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnTypeConstructorNew(type_ctor);
}

void LayoutParameterList::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& item : items) {
    visitor->OnLayoutParameter(item);
  }
}

void OrdinaledLayoutMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListNew(attributes);
  }

  visitor->OnOrdinal64(*ordinal);
  if (!reserved) {
    visitor->OnIdentifier(identifier);
  }
  if (type_ctor != nullptr) {
    visitor->OnTypeConstructorNew(type_ctor);
  }
}

void StructLayoutMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListNew(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructorNew(type_ctor);
  if (default_value != nullptr) {
    visitor->OnConstant(default_value);
  }
}

void ValueLayoutMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListNew(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnConstant(value);
}

void Layout::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  // TODO(fxbug.dev/68792): Parse attributes. Interestingly, we'll only want to
  //  do that in cases where the layout is defined inline on a layout member.

  if (modifiers != nullptr) {
    visitor->OnModifiers(modifiers);
  }
  if (subtype_ctor != nullptr) {
    visitor->OnTypeConstructorNew(subtype_ctor);
  }
  for (auto& member : members) {
    visitor->OnLayoutMember(member);
  }
}

void InlineLayoutReference::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeListNew(attributes);
  }
  visitor->OnLayout(layout);
}

void NamedLayoutReference::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnCompoundIdentifier(identifier);
}

void TypeConstraints::Accept(TreeVisitor* visitor) const {
  for (auto& item : items) {
    visitor->OnConstant(item);
  }
}

void TypeConstructorNew::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnLayoutReference(layout_ref);
  if (parameters != nullptr) {
    visitor->OnLayoutParameterList(parameters);
  }
  if (constraints != nullptr) {
    visitor->OnTypeConstraints(constraints);
  }
}

void TypeDecl::Accept(TreeVisitor* visitor) const {
  if (attributes != nullptr) {
    visitor->OnAttributeListNew(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructorNew(type_ctor);
}
// --- end new syntax ---

void File::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnLibraryDecl(library_decl);
  for (auto& i : using_list) {
    visitor->OnUsing(i);
  }
  for (auto& i : bits_declaration_list) {
    visitor->OnBitsDeclaration(i);
  }
  for (auto& i : const_declaration_list) {
    visitor->OnConstDeclaration(i);
  }
  for (auto& i : enum_declaration_list) {
    visitor->OnEnumDeclaration(i);
  }
  for (auto& i : protocol_declaration_list) {
    visitor->OnProtocolDeclaration(i);
  }
  for (auto& i : resource_declaration_list) {
    visitor->OnResourceDeclaration(i);
  }
  for (auto& i : service_declaration_list) {
    visitor->OnServiceDeclaration(i);
  }
  for (auto& i : struct_declaration_list) {
    visitor->OnStructDeclaration(i);
  }
  for (auto& i : table_declaration_list) {
    visitor->OnTableDeclaration(i);
  }
  for (auto& i : type_decls) {
    visitor->OnTypeDecl(i);
  }
  for (auto& i : union_declaration_list) {
    visitor->OnUnionDeclaration(i);
  }
}

}  // namespace raw
}  // namespace fidl
