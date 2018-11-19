// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the implementations of the Accept methods for the AST
// nodes.  Generally, all they do is invoke the appropriate TreeVisitor method
// for each field of the node.

#include "fidl/raw_ast.h"
#include "fidl/tree_visitor.h"

namespace fidl {
namespace raw {

SourceElementMark::SourceElementMark(TreeVisitor& tv,
                                     const SourceElement& element)
    : tv_(tv), element_(element) {
    tv_.OnSourceElementStart(element_);
}

SourceElementMark::~SourceElementMark() {
    tv_.OnSourceElementEnd(element_);
}

void CompoundIdentifier::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    for (auto i = components.begin(); i != components.end(); ++i) {
        visitor.OnIdentifier(*i);
    }
}

void StringLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void NumericLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void TrueLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void FalseLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void IdentifierConstant::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(identifier);
}

void LiteralConstant::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnLiteral(literal);
}

void Ordinal::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void Attribute::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void AttributeList::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    for (auto i = attributes.begin(); i != attributes.end(); ++i) {
        visitor.OnAttribute(*i);
    }
}

void ArrayType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnType(element_type);
    visitor.OnConstant(element_count);
}

void VectorType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnType(element_type);
    if (maybe_element_count != nullptr) {
        visitor.OnConstant(maybe_element_count);
    }
    visitor.OnNullability(nullability);
}

void StringType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (maybe_element_count != nullptr) {
        visitor.OnConstant(maybe_element_count);
    }

    visitor.OnNullability(nullability);
}

void HandleType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnHandleSubtype(subtype);
    visitor.OnNullability(nullability);
}

void RequestHandleType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(identifier);
    visitor.OnNullability(nullability);
}

void PrimitiveType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnPrimitiveSubtype(subtype);
}

void IdentifierType::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(identifier);
    visitor.OnNullability(nullability);
}

void Using::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(using_path);
    if (maybe_alias != nullptr) {
        visitor.OnIdentifier(maybe_alias);
    }
    if (maybe_primitive != nullptr) {
        visitor.OnPrimitiveType(maybe_primitive);
    }
}

void ConstDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnType(type);
    visitor.OnIdentifier(identifier);
    visitor.OnConstant(constant);
}

void EnumMember::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    visitor.OnConstant(value);
}

void EnumDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    if (maybe_subtype != nullptr) {
        visitor.OnPrimitiveType(maybe_subtype);
    }
    for (auto member = members.begin(); member != members.end(); ++member) {
        visitor.OnEnumMember(*member);
    }
}

void Parameter::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnType(type);
    visitor.OnIdentifier(identifier);
}

void ParameterList::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    for (auto parameter = parameter_list.begin(); parameter != parameter_list.end(); ++parameter) {
        visitor.OnParameter(*parameter);
    }
}

void InterfaceMethod::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    if (ordinal != nullptr) {
        visitor.OnOrdinal(*ordinal);
    }
    visitor.OnIdentifier(identifier);
    if (maybe_request != nullptr) {
        visitor.OnParameterList(maybe_request);
    }
    if (maybe_response != nullptr) {
        visitor.OnParameterList(maybe_response);
    }
}

void InterfaceDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    for (auto superinterface = superinterfaces.begin();
         superinterface != superinterfaces.end();
         ++superinterface) {
        visitor.OnCompoundIdentifier(*superinterface);
    }
    for (auto method = methods.begin();
         method != methods.end();
         ++method) {
        visitor.OnInterfaceMethod(*method);
    }
}

void StructMember::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnType(type);
    visitor.OnIdentifier(identifier);
    if (maybe_default_value != nullptr) {
        visitor.OnConstant(maybe_default_value);
    }
}

void StructDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor.OnStructMember(*member);
    }
}

void TableMember::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnOrdinal(*ordinal);
    if (maybe_used != nullptr) {
        if (maybe_used->attributes != nullptr) {
            visitor.OnAttributeList(maybe_used->attributes);
        }
        visitor.OnType(maybe_used->type);
        visitor.OnIdentifier(maybe_used->identifier);
        if (maybe_used->maybe_default_value != nullptr) {
            visitor.OnConstant(maybe_used->maybe_default_value);
        }
    }
}

void TableDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor.OnTableMember(*member);
    }
}

void UnionMember::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnType(type);
    visitor.OnIdentifier(identifier);
}

void UnionDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor.OnAttributeList(attributes);
    }
    visitor.OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor.OnUnionMember(*member);
    }
}

void File::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(library_name);
    for (auto i = using_list.begin();
         i != using_list.end();
         ++i) {
        visitor.OnUsing(*i);
    }
    for (auto i = const_declaration_list.begin();
         i != const_declaration_list.end();
         ++i) {
        visitor.OnConstDeclaration(*i);
    }
    for (auto i = enum_declaration_list.begin();
         i != enum_declaration_list.end();
         ++i) {
        visitor.OnEnumDeclaration(*i);
    }
    for (auto i = interface_declaration_list.begin();
         i != interface_declaration_list.end();
         ++i) {
        visitor.OnInterfaceDeclaration(*i);
    }
    for (auto i = struct_declaration_list.begin();
         i != struct_declaration_list.end();
         ++i) {
        visitor.OnStructDeclaration(*i);
    }
    for (auto i = table_declaration_list.begin();
         i != table_declaration_list.end();
         ++i) {
        visitor.OnTableDeclaration(*i);
    }
    for (auto i = union_declaration_list.begin();
         i != union_declaration_list.end();
         ++i) {
        visitor.OnUnionDeclaration(*i);
    }
}

} // namespace raw
} // namespace fidl
