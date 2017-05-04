// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump_visitor.h"

#include <stdio.h>

namespace fidl {

bool DumpVisitor::StartVisit(Identifier* identifier) {
    printf("%*.sIdentifier {\n", indent_, "");
    DumpStringView(identifier->identifier.data());
    Push();
    return true;
}

bool DumpVisitor::EndVisit(Identifier* identifier) {
    Pop();
    printf("%*.s} Identifier\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(CompoundIdentifier* compound_identifier) {
    printf("%*.sCompoundIdentifier {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(CompoundIdentifier* compound_identifier) {
    Pop();
    printf("%*.s} CompoundIdentifier\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StringLiteral* string_literal) {
    printf("%*.sStringLiteral {\n", indent_, "");
    DumpStringView(string_literal->literal.data());
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StringLiteral* string_literal) {
    Pop();
    printf("%*.s} StringLiteral\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(NumericLiteral* numeric_literal) {
    printf("%*.sNumericLiteral {\n", indent_, "");
    DumpStringView(numeric_literal->literal.data());
    Push();
    return true;
}

bool DumpVisitor::EndVisit(NumericLiteral* numeric_literal) {
    Pop();
    printf("%*.s} NumericLiteral\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(TrueLiteral* true_literal) {
    printf("%*.sTrueLiteral {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(TrueLiteral* true_literal) {
    Pop();
    printf("%*.s} TrueLiteral\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(FalseLiteral* false_literal) {
    printf("%*.sFalseLiteral {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(FalseLiteral* false_literal) {
    Pop();
    printf("%*.s} FalseLiteral\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(DefaultLiteral* default_literal) {
    printf("%*.sDefaultLiteral {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(DefaultLiteral* default_literal) {
    Pop();
    printf("%*.s} DefaultLiteral\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(HandleType* handle_type) {
    printf("%*.sHandleType {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(HandleType* handle_type) {
    Pop();
    printf("%*.s} HandleType\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(IdentifierType* identifier_type) {
    printf("%*.sIdentifierType {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(IdentifierType* identifier_type) {
    Pop();
    printf("%*.s} IdentifierType\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(PrimitiveType* primitive_type) {
    printf("%*.sPrimitiveType {\n", indent_, "");
    switch (primitive_type->type_kind) {
    case PrimitiveType::TypeKind::String:
        DumpStringView("String");
        break;
    case PrimitiveType::TypeKind::Bool:
        DumpStringView("Bool");
        break;
    case PrimitiveType::TypeKind::Int8:
        DumpStringView("Int8");
        break;
    case PrimitiveType::TypeKind::Int16:
        DumpStringView("Int16");
        break;
    case PrimitiveType::TypeKind::Int32:
        DumpStringView("Int32");
        break;
    case PrimitiveType::TypeKind::Int64:
        DumpStringView("Int64");
        break;
    case PrimitiveType::TypeKind::Uint8:
        DumpStringView("Uint8");
        break;
    case PrimitiveType::TypeKind::Uint16:
        DumpStringView("Uint16");
        break;
    case PrimitiveType::TypeKind::Uint32:
        DumpStringView("Uint32");
        break;
    case PrimitiveType::TypeKind::Uint64:
        DumpStringView("Uint64");
        break;
    case PrimitiveType::TypeKind::Float32:
        DumpStringView("Float32");
        break;
    case PrimitiveType::TypeKind::Float64:
        DumpStringView("Float64");
        break;
    }
    Push();
    return true;
}

bool DumpVisitor::EndVisit(PrimitiveType* primitive_type) {
    Pop();
    printf("%*.s} PrimitiveType\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(RequestType* request_type) {
    printf("%*.sRequestType {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(RequestType* request_type) {
    Pop();
    printf("%*.s} RequestType\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(IdentifierConstant* identifier_constant) {
    printf("%*.sIdentifierConstant {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(IdentifierConstant* identifier_constant) {
    Pop();
    printf("%*.s} IdentifierConstant\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(LiteralConstant* literal_constant) {
    printf("%*.sLiteralConstant {\n", indent_, "");

    Push();
    return true;
}

bool DumpVisitor::EndVisit(LiteralConstant* literal_constant) {
    Pop();

    printf("%*.s} LiteralConstant\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(ModuleName* module_name) {
    printf("%*.sModule {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(ModuleName* module_name) {
    Pop();
    printf("%*.s} ModuleName\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(Using* import) {
    printf("%*.sUsing {\n", indent_, "");

    Push();
    return true;
}

bool DumpVisitor::EndVisit(Using* import) {
    Pop();

    printf("%*.s} Using\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(UsingList* import_list) {
    printf("%*.sUsingList {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(UsingList* import_list) {
    Pop();
    printf("%*.s} UsingList\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(ConstDeclaration* const_declaration) {
    printf("%*.sConst {\n", indent_, "");

    Push();
    return true;
}

bool DumpVisitor::EndVisit(ConstDeclaration* const_declaration) {
    Pop();

    printf("%*.s} Const\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(EnumMemberValueIdentifier* enum_field_value_identifier) {
    printf("%*.sEnumMemberValueIdentifier {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(EnumMemberValueIdentifier* enum_field_value_identifier) {
    Pop();
    printf("%*.s} EnumMemberValueIdentifier\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(EnumMemberValueNumeric* enum_field_value_numeric) {
    printf("%*.sEnumMemberValueNumeric {\n", indent_, "");

    Push();
    return true;
}

bool DumpVisitor::EndVisit(EnumMemberValueNumeric* enum_field_value_numeric) {
    Pop();

    printf("%*.s} EnumMemberValueNumeric\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(EnumMember* enum_field) {
    printf("%*.sEnumMember {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(EnumMember* enum_field) {
    Pop();
    printf("%*.s} EnumMember\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(EnumBody* enum_body) {
    printf("%*.sEnumBody {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(EnumBody* enum_body) {
    Pop();
    printf("%*.s} EnumBody\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(EnumDeclaration* enum_declaration) {
    printf("%*.sEnum {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(EnumDeclaration* enum_declaration) {
    Pop();
    printf("%*.s} Enum\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(InterfaceMemberConst* interface_field_const) {
    printf("%*.sInterfaceMemberConst {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(InterfaceMemberConst* interface_field_const) {
    Pop();
    printf("%*.s} InterfaceMemberConst\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(InterfaceMemberEnum* interface_field_enum) {
    printf("%*.sInterfaceMemberEnum {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(InterfaceMemberEnum* interface_field_enum) {
    Pop();
    printf("%*.s} InterfaceMemberEnum\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(Parameter* parameter) {
    printf("%*.sParameter {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(Parameter* parameter) {
    Pop();
    printf("%*.s} Parameter\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(ParameterList* parameter_list) {
    printf("%*.sParameterList {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(ParameterList* parameter_list) {
    Pop();
    printf("%*.s} ParameterList\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(Response* response) {
    printf("%*.sResponse {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(Response* response) {
    Pop();
    printf("%*.s} Response\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(InterfaceMemberMethod* interface_field_method) {
    printf("%*.sInterfaceMemberMethod {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(InterfaceMemberMethod* interface_field_method) {
    Pop();
    printf("%*.s} InterfaceMemberMethod\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(InterfaceBody* interface_body) {
    printf("%*.sInterfaceBody {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(InterfaceBody* interface_body) {
    Pop();
    printf("%*.s} InterfaceBody\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(InterfaceDeclaration* interface_declaration) {
    printf("%*.sInterface {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(InterfaceDeclaration* interface_declaration) {
    Pop();
    printf("%*.s} Interface\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructMemberConst* struct_field_const) {
    printf("%*.sStructMemberConst {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructMemberConst* struct_field_const) {
    Pop();
    printf("%*.s} StructMemberConst\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructMemberEnum* struct_field_enum) {
    printf("%*.sStructMemberEnum {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructMemberEnum* struct_field_enum) {
    Pop();
    printf("%*.s} StructMemberEnum\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructDefaultValue* struct_default_value) {
    printf("%*.sStructDefaultValue {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructDefaultValue* struct_default_value) {
    Pop();
    printf("%*.s} StructDefaultValue\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructMemberField* struct_field_field) {
    printf("%*.sStructMemberField {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructMemberField* struct_field_field) {
    Pop();
    printf("%*.s} StructMemberField\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructBody* struct_body) {
    printf("%*.sStructBody {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructBody* struct_body) {
    Pop();
    printf("%*.s} StructBody\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(StructDeclaration* struct_declaration) {
    printf("%*.sStruct {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(StructDeclaration* struct_declaration) {
    Pop();
    printf("%*.s} Struct\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(UnionMember* union_field) {
    printf("%*.sUnionMember {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(UnionMember* union_field) {
    Pop();
    printf("%*.s} UnionMember\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(UnionBody* union_body) {
    printf("%*.sUnionBody {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(UnionBody* union_body) {
    Pop();
    printf("%*.s} UnionBody\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(UnionDeclaration* union_declaration) {
    printf("%*.sUnion {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(UnionDeclaration* union_declaration) {
    Pop();
    printf("%*.s} Union\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(DeclarationList* declaration_list) {
    printf("%*.sDeclarationList {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(DeclarationList* declaration_list) {
    Pop();
    printf("%*.s} DeclarationList\n", indent_, "");
    return true;
}

bool DumpVisitor::StartVisit(File* file) {
    printf("%*.sFile {\n", indent_, "");
    Push();
    return true;
}

bool DumpVisitor::EndVisit(File* file) {
    Pop();
    printf("%*.s} File\n", indent_, "");
    return true;
}

void DumpVisitor::DumpStringView(StringView data) {
    Push();
    printf("%*.s(%zu) |%.*s|\n", indent_, "", data.size(), static_cast<int>(data.size()), data.data());
    Pop();
}

} // namespace fidl
