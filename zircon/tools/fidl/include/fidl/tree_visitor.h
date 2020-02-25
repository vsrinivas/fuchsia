// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_ast.h"

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TREE_VISITOR_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TREE_VISITOR_H_

namespace fidl {
namespace raw {

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
    fidl::raw::Literal::Kind kind = element->kind;
    switch (kind) {
      case Literal::Kind::kString: {
        StringLiteral* literal = static_cast<StringLiteral*>(element.get());
        OnStringLiteral(*literal);
        break;
      }
      case Literal::Kind::kNumeric: {
        NumericLiteral* literal = static_cast<NumericLiteral*>(element.get());
        OnNumericLiteral(*literal);
        break;
      }
      case Literal::Kind::kTrue: {
        TrueLiteral* literal = static_cast<TrueLiteral*>(element.get());
        OnTrueLiteral(*literal);
        break;
      }
      case Literal::Kind::kFalse: {
        FalseLiteral* literal = static_cast<FalseLiteral*>(element.get());
        OnFalseLiteral(*literal);
        break;
      }
      default:
        // Die!
        break;
    }
  }
  virtual void OnStringLiteral(StringLiteral& element) { element.Accept(this); }

  virtual void OnNumericLiteral(NumericLiteral& element) { element.Accept(this); }

  virtual void OnTrueLiteral(TrueLiteral& element) { element.Accept(this); }

  virtual void OnFalseLiteral(FalseLiteral& element) { element.Accept(this); }

  virtual void OnOrdinal32(Ordinal32& element) { element.Accept(this); }

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
    uptr.release();                                        \
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

  virtual void OnAttribute(const Attribute& element) { element.Accept(this); }

  virtual void OnAttributeList(std::unique_ptr<AttributeList> const& element) {
    element->Accept(this);
  }

  virtual void OnTypeConstructor(std::unique_ptr<TypeConstructor> const& element) {
    element->Accept(this);
  }

  virtual void OnUsing(std::unique_ptr<Using> const& element) { element->Accept(this); }

  virtual void OnConstDeclaration(std::unique_ptr<ConstDeclaration> const& element) {
    element->Accept(this);
  }

  virtual void OnBitsMember(std::unique_ptr<BitsMember> const& element) { element->Accept(this); }
  virtual void OnBitsDeclaration(std::unique_ptr<BitsDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnEnumMember(std::unique_ptr<EnumMember> const& element) { element->Accept(this); }
  virtual void OnEnumDeclaration(std::unique_ptr<EnumDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnParameter(std::unique_ptr<Parameter> const& element) { element->Accept(this); }
  virtual void OnParameterList(std::unique_ptr<ParameterList> const& element) {
    element->Accept(this);
  }
  virtual void OnProtocolMethod(std::unique_ptr<ProtocolMethod> const& element) {
    element->Accept(this);
  }
  virtual void OnComposeProtocol(std::unique_ptr<ComposeProtocol> const& element) {
    element->Accept(this);
  }
  virtual void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnServiceMember(std::unique_ptr<ServiceMember> const& element) {
    element->Accept(this);
  }
  virtual void OnServiceDeclaration(std::unique_ptr<ServiceDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnStructMember(std::unique_ptr<StructMember> const& element) {
    element->Accept(this);
  }
  virtual void OnStructDeclaration(std::unique_ptr<StructDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnTableMember(std::unique_ptr<TableMember> const& element) { element->Accept(this); }
  virtual void OnTableDeclaration(std::unique_ptr<TableDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnUnionMember(std::unique_ptr<UnionMember> const& element) { element->Accept(this); }
  virtual void OnUnionDeclaration(std::unique_ptr<UnionDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnXUnionMember(std::unique_ptr<XUnionMember> const& element) {
    element->Accept(this);
  }
  virtual void OnXUnionDeclaration(std::unique_ptr<XUnionDeclaration> const& element) {
    element->Accept(this);
  }
  virtual void OnFile(std::unique_ptr<File> const& element) { element->Accept(this); }
  virtual void OnHandleSubtype(types::HandleSubtype subtype) {}
  virtual void OnPrimitiveSubtype(types::PrimitiveSubtype subtype) {}
  virtual void OnNullability(types::Nullability nullability) {}
};

#undef DISPATCH_TO

// AST node contents are not stored in declaration order in the tree, so we
// have a special visitor for code that needs to visit in declaration order.
class DeclarationOrderTreeVisitor : public TreeVisitor {
 public:
  virtual void OnFile(std::unique_ptr<File> const& element) override;
  virtual void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const& element) override;
};

}  // namespace raw
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TREE_VISITOR_H_
