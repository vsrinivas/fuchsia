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

void Attribute::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : args) {
    visitor->OnAttributeArg(i);
  }
}

void AttributeList::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  for (auto& i : attributes) {
    visitor->OnAttribute(i);
  }
}

void LibraryDecl::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(path);
}

void Using::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(using_path);
  if (maybe_alias != nullptr) {
    visitor->OnIdentifier(maybe_alias);
  }
}

void AliasDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(alias);
  visitor->OnTypeConstructor(type_ctor);
}

void ConstDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnTypeConstructor(type_ctor);
  visitor->OnIdentifier(identifier);
  visitor->OnConstant(constant);
}

void ParameterList::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (type_ctor) {
    visitor->OnTypeConstructor(type_ctor);
  }
}

void ProtocolMethod::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (maybe_request != nullptr) {
    visitor->OnParameterList(maybe_request);
  }
  if (maybe_response != nullptr) {
    visitor->OnParameterList(maybe_response);
  }
  if (maybe_error_ctor != nullptr) {
    visitor->OnTypeConstructor(maybe_error_ctor);
  }
}

void ProtocolCompose::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnCompoundIdentifier(protocol_name);
}

void ProtocolDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
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
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructor(type_ctor);
}

void ResourceDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  if (maybe_type_ctor != nullptr) {
    visitor->OnTypeConstructor(maybe_type_ctor);
  }
  for (auto property = properties.begin(); property != properties.end(); ++property) {
    visitor->OnResourceProperty(*property);
  }
}

void ServiceMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructor(type_ctor);
}

void ServiceDeclaration::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }
  visitor->OnIdentifier(identifier);
  for (auto member = members.begin(); member != members.end(); ++member) {
    visitor->OnServiceMember(*member);
  }
}

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
  visitor->OnTypeConstructor(type_ctor);
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
    visitor->OnAttributeList(attributes);
  }

  visitor->OnOrdinal64(*ordinal);
  if (!reserved) {
    visitor->OnIdentifier(identifier);
  }
  if (type_ctor != nullptr) {
    visitor->OnTypeConstructor(type_ctor);
  }
}

void StructLayoutMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructor(type_ctor);
  if (default_value != nullptr) {
    visitor->OnConstant(default_value);
  }
}

void ValueLayoutMember::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnConstant(value);
}

void Layout::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (modifiers != nullptr) {
    visitor->OnModifiers(modifiers);
  }
  if (subtype_ctor != nullptr) {
    visitor->OnTypeConstructor(subtype_ctor);
  }
  for (auto& member : members) {
    visitor->OnLayoutMember(member);
  }
}

void InlineLayoutReference::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  if (attributes != nullptr) {
    visitor->OnAttributeList(attributes);
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

void TypeConstructor::Accept(TreeVisitor* visitor) const {
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
    visitor->OnAttributeList(attributes);
  }

  visitor->OnIdentifier(identifier);
  visitor->OnTypeConstructor(type_ctor);
}

void File::Accept(TreeVisitor* visitor) const {
  SourceElementMark sem(visitor, *this);
  visitor->OnLibraryDecl(library_decl);
  for (auto& i : using_list) {
    visitor->OnUsing(i);
  }
  for (auto& i : const_declaration_list) {
    visitor->OnConstDeclaration(i);
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
  for (auto& i : type_decls) {
    visitor->OnTypeDecl(i);
  }
}

}  // namespace raw
}  // namespace fidl
