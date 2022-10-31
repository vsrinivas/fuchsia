// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/raw_ast.h"

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TREE_VISITOR_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TREE_VISITOR_H_

namespace fidl::raw {

// A TreeVisitor is an API that walks a FIDL AST.  The default implementation
// does nothing but walk the AST.  To make it interesting, subclass TreeVisitor
// and override behaviors with the ones you want.
class TreeVisitor {
 public:
  virtual ~TreeVisitor() = default;

  virtual void OnSourceElementStart(const SourceElement& element) {}
  virtual void OnSourceElementEnd(const SourceElement& element) {}
  virtual void OnIdentifier(std::unique_ptr<Identifier> const& element) { element->Accept(this); }
  virtual void OnCompoundIdentifier(std::unique_ptr<CompoundIdentifier> const& element) {
    element->Accept(this);
  }

  virtual void OnLiteral(std::unique_ptr<fidl::raw::Literal> const& element) {
    switch (element->kind) {
      case Literal::Kind::kDocComment:
        OnDocCommentLiteral(*static_cast<DocCommentLiteral*>(element.get()));
        break;
      case Literal::Kind::kString:
        OnStringLiteral(*static_cast<StringLiteral*>(element.get()));
        break;
      case Literal::Kind::kNumeric:
        OnNumericLiteral(*static_cast<NumericLiteral*>(element.get()));
        break;
      case Literal::Kind::kBool:
        OnBoolLiteral(*static_cast<BoolLiteral*>(element.get()));
        break;
    }
  }
  virtual void OnDocCommentLiteral(DocCommentLiteral& element) { element.Accept(this); }

  virtual void OnStringLiteral(StringLiteral& element) { element.Accept(this); }

  virtual void OnNumericLiteral(NumericLiteral& element) { element.Accept(this); }

  virtual void OnBoolLiteral(BoolLiteral& element) { element.Accept(this); }

  virtual void OnOrdinal64(Ordinal64& element) { element.Accept(this); }

#ifdef DISPATCH_TO
#error "Cannot define macro DISPATCH_TO: already defined"
#endif
#define DISPATCH_TO(TYPE, SUPERTYPE, ELEMENT)              \
  do {                                                     \
    std::unique_ptr<SUPERTYPE>& unconst_element =          \
        const_cast<std::unique_ptr<SUPERTYPE>&>(element);  \
    TYPE* ptr = static_cast<TYPE*>(unconst_element.get()); \
    std::unique_ptr<TYPE> uptr(ptr);                       \
    On##TYPE(uptr);                                        \
    static_cast<void>(uptr.release());                     \
  } while (0)

  virtual void OnConstant(std::unique_ptr<Constant> const& element) {
    Constant::Kind kind = element->kind;
    switch (kind) {
      case Constant::Kind::kIdentifier: {
        DISPATCH_TO(IdentifierConstant, Constant, element);
        break;
      }
      case Constant::Kind::kLiteral: {
        DISPATCH_TO(LiteralConstant, Constant, element);
        break;
      }
      case Constant::Kind::kBinaryOperator: {
        DISPATCH_TO(BinaryOperatorConstant, Constant, element);
        break;
      }
    }
  }

  virtual void OnIdentifierConstant(std::unique_ptr<IdentifierConstant> const& element) {
    element->Accept(this);
  }
  virtual void OnLiteralConstant(std::unique_ptr<LiteralConstant> const& element) {
    element->Accept(this);
  }
  virtual void OnBinaryOperatorConstant(std::unique_ptr<BinaryOperatorConstant> const& element) {
    element->Accept(this);
  }

  virtual void OnAttributeArg(const std::unique_ptr<AttributeArg>& element) {
    element->Accept(this);
  }

  virtual void OnAttribute(const std::unique_ptr<Attribute>& element) { element->Accept(this); }

  virtual void OnAttributeList(std::unique_ptr<AttributeList> const& element) {
    element->Accept(this);
  }

  virtual void OnAliasDeclaration(std::unique_ptr<AliasDeclaration> const& element) {
    element->Accept(this);
  }

  virtual void OnLibraryDecl(std::unique_ptr<LibraryDecl> const& element) { element->Accept(this); }

  virtual void OnUsing(std::unique_ptr<Using> const& element) { element->Accept(this); }

  virtual void OnConstDeclaration(std::unique_ptr<ConstDeclaration> const& element) {
    element->Accept(this);
  }

  virtual void OnParameterList(std::unique_ptr<ParameterList> const& element) {
    element->Accept(this);
  }
  virtual void OnProtocolMethod(std::unique_ptr<ProtocolMethod> const& element) {
    element->Accept(this);
  }
  virtual void OnProtocolCompose(std::unique_ptr<ProtocolCompose> const& element) {
    element->Accept(this);
  }
  virtual void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnResourceProperty(std::unique_ptr<ResourceProperty> const& element) {
    element->Accept(this);
  }
  virtual void OnResourceDeclaration(std::unique_ptr<ResourceDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnServiceMember(std::unique_ptr<ServiceMember> const& element) {
    element->Accept(this);
  }
  virtual void OnServiceDeclaration(std::unique_ptr<ServiceDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnModifiers(std::unique_ptr<Modifiers> const& element) { element->Accept(this); }

  virtual void OnLayoutParameter(std::unique_ptr<LayoutParameter> const& element) {
    LayoutParameter::Kind kind = element->kind;
    switch (kind) {
      case LayoutParameter::Kind::kIdentifier: {
        DISPATCH_TO(IdentifierLayoutParameter, LayoutParameter, element);
        break;
      }
      case LayoutParameter::Kind::kLiteral: {
        DISPATCH_TO(LiteralLayoutParameter, LayoutParameter, element);
        break;
      }
      case LayoutParameter::Kind::kType: {
        DISPATCH_TO(TypeLayoutParameter, LayoutParameter, element);
        break;
      }
    }
  }

  virtual void OnLayoutParameterList(std::unique_ptr<LayoutParameterList> const& element) {
    element->Accept(this);
  }

  virtual void OnIdentifierLayoutParameter(
      std::unique_ptr<IdentifierLayoutParameter> const& element) {
    element->Accept(this);
  }
  virtual void OnLiteralLayoutParameter(std::unique_ptr<LiteralLayoutParameter> const& element) {
    element->Accept(this);
  }
  virtual void OnTypeLayoutParameter(std::unique_ptr<TypeLayoutParameter> const& element) {
    element->Accept(this);
  }

  virtual void OnLayoutMember(std::unique_ptr<LayoutMember> const& element) {
    LayoutMember::Kind kind = element->kind;
    switch (kind) {
      case LayoutMember::Kind::kOrdinaled: {
        DISPATCH_TO(OrdinaledLayoutMember, LayoutMember, element);
        break;
      }
      case LayoutMember::Kind::kStruct: {
        DISPATCH_TO(StructLayoutMember, LayoutMember, element);
        break;
      }
      case LayoutMember::Kind::kValue: {
        DISPATCH_TO(ValueLayoutMember, LayoutMember, element);
        break;
      }
    }
  }

  virtual void OnOrdinaledLayoutMember(std::unique_ptr<OrdinaledLayoutMember> const& element) {
    element->Accept(this);
  }
  virtual void OnStructLayoutMember(std::unique_ptr<StructLayoutMember> const& element) {
    element->Accept(this);
  }
  virtual void OnValueLayoutMember(std::unique_ptr<ValueLayoutMember> const& element) {
    element->Accept(this);
  }

  virtual void OnLayout(std::unique_ptr<Layout> const& element) { element->Accept(this); }

  virtual void OnLayoutReference(std::unique_ptr<LayoutReference> const& element) {
    LayoutReference::Kind kind = element->kind;
    switch (kind) {
      case LayoutReference::Kind::kInline: {
        DISPATCH_TO(InlineLayoutReference, LayoutReference, element);
        break;
      }
      case LayoutReference::Kind::kNamed: {
        DISPATCH_TO(NamedLayoutReference, LayoutReference, element);
        break;
      }
    }
  }

  virtual void OnInlineLayoutReference(std::unique_ptr<InlineLayoutReference> const& element) {
    element->Accept(this);
  }
  virtual void OnNamedLayoutReference(std::unique_ptr<NamedLayoutReference> const& element) {
    element->Accept(this);
  }

  virtual void OnTypeConstraints(std::unique_ptr<TypeConstraints> const& element) {
    element->Accept(this);
  }

  virtual void OnTypeConstructor(std::unique_ptr<TypeConstructor> const& element) {
    element->Accept(this);
  }

  virtual void OnTypeDecl(std::unique_ptr<TypeDecl> const& element) { element->Accept(this); }
  // --- end new syntax ---

  virtual void OnFile(std::unique_ptr<File> const& element) { element->Accept(this); }
  virtual void OnPrimitiveSubtype(types::PrimitiveSubtype subtype) {}
  virtual void OnNullability(types::Nullability nullability) {}
};

#undef DISPATCH_TO

// AST node contents are not stored in declaration order in the tree, so we
// have a special visitor for code that needs to visit in declaration order.
class DeclarationOrderTreeVisitor : public TreeVisitor {
 public:
  void OnFile(std::unique_ptr<File> const& element) override;
  void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const& element) override;
};

}  // namespace fidl::raw

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TREE_VISITOR_H_
