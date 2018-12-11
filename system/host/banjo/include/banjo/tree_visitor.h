// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_ast.h"

#ifndef ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_TREE_VISITOR_H_
#define ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_TREE_VISITOR_H_

namespace banjo {
namespace raw {

// A TreeVisitor is an API that walks a BANJO AST.  The default implementation
// does nothing but walk the AST.  To make it interesting, subclass TreeVisitor
// and override behaviors with the ones you want.
class TreeVisitor {
public:
    virtual void OnSourceElementStart(const SourceElement& element) {
    }
    virtual void OnSourceElementEnd(const SourceElement& element) {
    }
    virtual void OnIdentifier(std::unique_ptr<Identifier> const& element) {
        element->Accept(*this);
    }
    virtual void OnCompoundIdentifier(std::unique_ptr<CompoundIdentifier> const& element) {
        element->Accept(*this);
    }

    virtual void OnLiteral(std::unique_ptr<banjo::raw::Literal> const& element) {
        banjo::raw::Literal::Kind kind = element->kind;
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
    virtual void OnStringLiteral(StringLiteral& element) {
        element.Accept(*this);
    }

    virtual void OnNumericLiteral(NumericLiteral& element) {
        element.Accept(*this);
    }

    virtual void OnTrueLiteral(TrueLiteral& element) {
        element.Accept(*this);
    }

    virtual void OnFalseLiteral(FalseLiteral& element) {
        element.Accept(*this);
    }

#ifdef DISPATCH_TO
#error "Cannot define macro DISPATCH_TO: already defined"
#endif
#define DISPATCH_TO(TYPE, SUPERTYPE, ELEMENT)                  \
    do {                                                       \
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
        }
    }

    virtual void OnIdentifierConstant(std::unique_ptr<IdentifierConstant> const& element) {
        element->Accept(*this);
    }
    virtual void OnLiteralConstant(std::unique_ptr<LiteralConstant> const& element) {
        element->Accept(*this);
    }

    virtual void OnAttribute(std::unique_ptr<Attribute>& element) {
        element->Accept(*this);
    }

    virtual void OnAttributeList(std::unique_ptr<AttributeList> const& element) {
        element->Accept(*this);
    }

    virtual void OnType(std::unique_ptr<Type> const& element) {
        banjo::raw::Type::Kind kind = element->kind;
        switch (kind) {
        case Type::Kind::kArray: {
            DISPATCH_TO(ArrayType, Type, element);
            break;
        }
        case Type::Kind::kVector: {
            DISPATCH_TO(VectorType, Type, element);
            break;
        }
        case Type::Kind::kString: {
            DISPATCH_TO(StringType, Type, element);
            break;
        }
        case Type::Kind::kHandle: {
            DISPATCH_TO(HandleType, Type, element);
            break;
        }
        case Type::Kind::kRequestHandle: {
            DISPATCH_TO(RequestHandleType, Type, element);
            break;
        }
        case Type::Kind::kPrimitive: {
            DISPATCH_TO(PrimitiveType, Type, element);
            break;
        }
        case Type::Kind::kIdentifier: {
            DISPATCH_TO(IdentifierType, Type, element);
            break;
        }
        default:
            // Die!
            break;
        }
        return;
    }
    virtual void OnArrayType(std::unique_ptr<ArrayType> const& element) {
        element->Accept(*this);
    }
    virtual void OnVectorType(std::unique_ptr<VectorType> const& element) {
        element->Accept(*this);
    }
    virtual void OnStringType(std::unique_ptr<StringType> const& element) {
        element->Accept(*this);
    }
    virtual void OnHandleType(std::unique_ptr<HandleType> const& element) {
        element->Accept(*this);
    }
    virtual void OnRequestHandleType(std::unique_ptr<RequestHandleType> const& element) {
        element->Accept(*this);
    }
    virtual void OnPrimitiveType(std::unique_ptr<PrimitiveType> const& element) {
        element->Accept(*this);
    }
    virtual void OnIdentifierType(std::unique_ptr<IdentifierType> const& element) {
        element->Accept(*this);
    }
    virtual void OnUsing(std::unique_ptr<Using> const& element) {
        element->Accept(*this);
    }

    virtual void OnConstDeclaration(std::unique_ptr<ConstDeclaration> const& element) {
        element->Accept(*this);
    }

    virtual void OnEnumMember(std::unique_ptr<EnumMember> const& element) {
        element->Accept(*this);
    }
    virtual void OnEnumDeclaration(std::unique_ptr<EnumDeclaration> const& element) {
        element->Accept(*this);
    }
    virtual void OnParameter(std::unique_ptr<Parameter> const& element) {
        element->Accept(*this);
    }
    virtual void OnParameterList(std::unique_ptr<ParameterList> const& element) {
        element->Accept(*this);
    }
    virtual void OnInterfaceMethod(std::unique_ptr<InterfaceMethod> const& element) {
        element->Accept(*this);
    }
    virtual void OnInterfaceDeclaration(std::unique_ptr<InterfaceDeclaration> const& element) {
        element->Accept(*this);
    }
    virtual void OnStructMember(std::unique_ptr<StructMember> const& element) {
        element->Accept(*this);
    }
    virtual void OnStructDeclaration(std::unique_ptr<StructDeclaration> const& element) {
        element->Accept(*this);
    }
    virtual void OnUnionMember(std::unique_ptr<UnionMember> const& element) {
        element->Accept(*this);
    }
    virtual void OnUnionDeclaration(std::unique_ptr<UnionDeclaration> const& element) {
        element->Accept(*this);
    }
    virtual void OnFile(std::unique_ptr<File> const& element) {
        element->Accept(*this);
    }
    virtual void OnHandleSubtype(types::HandleSubtype subtype) {
    }
    virtual void OnPrimitiveSubtype(types::PrimitiveSubtype subtype) {
    }
    virtual void OnNullability(types::Nullability nullability) {
    }
};

#undef DISPATCH_TO

// AST node contents are not stored in declaration order in the tree, so we
// have a special visitor for code that needs to visit in declaration order.
class DeclarationOrderTreeVisitor : public TreeVisitor {
public:
    virtual void OnFile(std::unique_ptr<File> const& element) override;
};

} // namespace raw
} // namespace banjo

#endif // ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_TREE_VISITOR_H_
