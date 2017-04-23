// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "c_header_visitor.h"

#include <assert.h>
#include <stdio.h>

namespace fidl {

namespace {

void PrintToken(Token token) {
    printf("%.*s", static_cast<int>(token.data().size()), token.data().data());
}

void PrintInterfaceHeader(int indent) {
    printf("%*.smx_txid_t txid;\n", indent, "");
    printf("%*.suint32_t flags;\n", indent, "");
    printf("%*.suint32_t ordinal;\n", indent, "");
}

const char* PrimitiveTypeName(PrimitiveType::TypeKind kind) {
    switch (kind) {
    case PrimitiveType::TypeKind::String:
        return "FidlString";

    case PrimitiveType::TypeKind::Bool:
        return "bool";

    case PrimitiveType::TypeKind::Int8:
        return "int8_t";

    case PrimitiveType::TypeKind::Int16:
        return "int16_t";

    case PrimitiveType::TypeKind::Int32:
        return "int32_t";

    case PrimitiveType::TypeKind::Int64:
        return "int64_t";

    case PrimitiveType::TypeKind::Uint8:
        return "uint8_t";

    case PrimitiveType::TypeKind::Uint16:
        return "uint16_t";

    case PrimitiveType::TypeKind::Uint32:
        return "uint32_t";

    case PrimitiveType::TypeKind::Uint64:
        return "uint64_t";

    case PrimitiveType::TypeKind::Float32:
        return "float";

    case PrimitiveType::TypeKind::Float64:
        return "double";
    }
}

void PrintHandleType(HandleType* handle_type) {
    printf("mx_handle_t");
}

void PrintIdentifierType(IdentifierType* identifier_type) {
    printf("IDENTIFIER_TYPE");
}

void PrintPrimitiveType(PrimitiveType* primitive_type) {
    printf("%s", PrimitiveTypeName(primitive_type->type_kind));
}

void PrintRequestType(RequestType* request_type) {
    printf("REQUEST_TYPE");
}

void PrintType(Type* type) {
    if (HandleType* handle_type = try_cast<HandleType>(type))
        PrintHandleType(handle_type);
    else if (IdentifierType* identifier_type = try_cast<IdentifierType>(type))
        PrintIdentifierType(identifier_type);
    else if (PrimitiveType* primitive_type = try_cast<PrimitiveType>(type))
        PrintPrimitiveType(primitive_type);
    else if (RequestType* request_type = try_cast<RequestType>(type))
        PrintRequestType(request_type);
    else
        assert(false);
}

void PrintAggregateField(int indent, Type* type, Identifier* identifier) {
    printf("%*.s", indent, "");
    PrintType(type);
    printf(" ");
    PrintToken(identifier->identifier);
    printf(";");
    if (HandleType* handle_type = try_cast<HandleType>(type)) {
        if (handle_type->maybe_subtype) {
            printf(" // ");
            PrintToken(handle_type->maybe_subtype->identifier);
        }
    }
    printf("\n");
}

void StartAggregate(int indent, const char* tag, Token* name, const char* suffix) {
    printf("\n%*.s%s", indent, "", tag);
    if (name) {
        printf(" ");
        PrintToken(*name);
    }
    printf("%s {\n", suffix);
}

void EndAggregate(int indent) {
    printf("%*.s};\n", indent, "");
}

} // namespace

bool CHeaderVisitor::StartVisit(CompoundIdentifier* compound_identifier) {
    return true;
}

bool CHeaderVisitor::EndVisit(CompoundIdentifier* compound_identifier) {
    return true;
}

bool CHeaderVisitor::StartVisit(HandleType* handle_type) {
    return true;
}

bool CHeaderVisitor::EndVisit(HandleType* handle_type) {
    return true;
}

bool CHeaderVisitor::StartVisit(IdentifierType* identifier_type) {
    return true;
}

bool CHeaderVisitor::EndVisit(IdentifierType* identifier_type) {
    return true;
}

bool CHeaderVisitor::StartVisit(PrimitiveType* primitive_type) {
    return true;
}

bool CHeaderVisitor::EndVisit(PrimitiveType* primitive_type) {
    return true;
}

bool CHeaderVisitor::StartVisit(RequestType* request_type) {
    return true;
}

bool CHeaderVisitor::EndVisit(RequestType* request_type) {
    return true;
}

bool CHeaderVisitor::StartVisit(IdentifierConstant* identifier_constant) {
    return true;
}

bool CHeaderVisitor::EndVisit(IdentifierConstant* identifier_constant) {
    return true;
}

bool CHeaderVisitor::StartVisit(LiteralConstant* literal_constant) {
    return true;
}

bool CHeaderVisitor::EndVisit(LiteralConstant* literal_constant) {
    return true;
}

bool CHeaderVisitor::StartVisit(Module* module) {
    return true;
}

bool CHeaderVisitor::EndVisit(Module* module) {
    return true;
}

bool CHeaderVisitor::StartVisit(Using* import) {
    return true;
}

bool CHeaderVisitor::EndVisit(Using* import) {
    return true;
}

bool CHeaderVisitor::StartVisit(UsingList* import_list) {
    return true;
}

bool CHeaderVisitor::EndVisit(UsingList* import_list) {
    return true;
}

bool CHeaderVisitor::StartVisit(ConstDeclaration* const_declaration) {
    return true;
}

bool CHeaderVisitor::EndVisit(ConstDeclaration* const_declaration) {
    return true;
}

bool CHeaderVisitor::StartVisit(EnumMemberValueIdentifier* enum_field_value_identifier) {
    return true;
}

bool CHeaderVisitor::EndVisit(EnumMemberValueIdentifier* enum_field_value_identifier) {
    return true;
}

bool CHeaderVisitor::StartVisit(EnumMemberValueNumeric* enum_field_value_numeric) {
    return true;
}

bool CHeaderVisitor::EndVisit(EnumMemberValueNumeric* enum_field_value_numeric) {
    PrintToken(enum_field_value_numeric->literal->literal);

    return true;
}

bool CHeaderVisitor::StartVisit(EnumMember* enum_field) {
    printf("%*.s#define ", indent_, "");
    PrintToken(enum_field->identifier->identifier);
    printf(" %s(", current_enum_cast_);

    return true;
}

bool CHeaderVisitor::EndVisit(EnumMember* enum_field) {
    printf(")\n");

    return true;
}

bool CHeaderVisitor::StartVisit(EnumDeclaration* enum_declaration) {
    auto subtype = PrimitiveType::TypeKind::Uint32;
    if (enum_declaration->maybe_subtype)
        subtype = enum_declaration->maybe_subtype->type_kind;

    switch (subtype) {
    case PrimitiveType::TypeKind::Int8:
        current_enum_cast_ = "INT8_C";
        break;

    case PrimitiveType::TypeKind::Int16:
        current_enum_cast_ = "INT16_C";
        break;

    case PrimitiveType::TypeKind::Int32:
        current_enum_cast_ = "INT32_C";
        break;

    case PrimitiveType::TypeKind::Int64:
        current_enum_cast_ = "INT64_C";
        break;

    case PrimitiveType::TypeKind::Uint8:
        current_enum_cast_ = "UINT8_C";
        break;

    case PrimitiveType::TypeKind::Uint16:
        current_enum_cast_ = "UINT16_C";
        break;

    case PrimitiveType::TypeKind::Uint32:
        current_enum_cast_ = "UINT32_C";
        break;

    case PrimitiveType::TypeKind::Uint64:
        current_enum_cast_ = "UINT64_C";
        break;

    default:
        // Non-integral primitive types should not be reachable here.
        assert(false);
        return false;
    }

    printf("%*.stypedef %s ", indent_, "", PrimitiveTypeName(subtype));
    PrintToken(enum_declaration->identifier->identifier);
    printf(";\n");

    return true;
}

bool CHeaderVisitor::EndVisit(EnumDeclaration* enum_declaration) {
    printf("\n");

    return true;
}

bool CHeaderVisitor::StartVisit(InterfaceMemberConst* interface_field_const) {
    return true;
}

bool CHeaderVisitor::EndVisit(InterfaceMemberConst* interface_field_const) {
    return true;
}

bool CHeaderVisitor::StartVisit(InterfaceMemberEnum* interface_field_enum) {
    return true;
}

bool CHeaderVisitor::EndVisit(InterfaceMemberEnum* interface_field_enum) {
    return true;
}

bool CHeaderVisitor::StartVisit(Parameter* parameter) {
    PrintAggregateField(indent_, parameter->type.get(), parameter->identifier.get());

    return true;
}

bool CHeaderVisitor::EndVisit(Parameter* parameter) {
    return true;
}

bool CHeaderVisitor::StartVisit(Response* response) {
    Pop();
    EndAggregate(indent_);

    StartAggregate(indent_, "struct", &current_method_name_, "Response");
    Push();

    PrintInterfaceHeader(indent_);

    return true;
}

bool CHeaderVisitor::EndVisit(Response* response) {
    return true;
}

bool CHeaderVisitor::StartVisit(InterfaceMemberMethod* interface_field_method) {
    current_method_name_ = interface_field_method->identifier->identifier;
    StartAggregate(indent_, "struct", &current_method_name_, "Call");
    Push();

    PrintInterfaceHeader(indent_);

    return true;
}

bool CHeaderVisitor::EndVisit(InterfaceMemberMethod* interface_field_method) {
    Pop();
    EndAggregate(indent_);

    return true;
}

bool CHeaderVisitor::StartVisit(InterfaceDeclaration* interface_declaration) {
    return true;
}

bool CHeaderVisitor::EndVisit(InterfaceDeclaration* interface_declaration) {
    return true;
}

bool CHeaderVisitor::StartVisit(StructMemberConst* struct_field_const) {
    return true;
}

bool CHeaderVisitor::EndVisit(StructMemberConst* struct_field_const) {
    return true;
}

bool CHeaderVisitor::StartVisit(StructMemberEnum* struct_field_enum) {
    return true;
}

bool CHeaderVisitor::EndVisit(StructMemberEnum* struct_field_enum) {
    return true;
}

bool CHeaderVisitor::StartVisit(StructDefaultValue* struct_default_value) {
    return true;
}

bool CHeaderVisitor::EndVisit(StructDefaultValue* struct_default_value) {
    return true;
}

bool CHeaderVisitor::StartVisit(StructMemberField* struct_field_field) {
    return true;
}

bool CHeaderVisitor::EndVisit(StructMemberField* struct_field_field) {
    return true;
}

bool CHeaderVisitor::StartVisit(StructDeclaration* struct_declaration) {
    auto name = struct_declaration->identifier->identifier;
    StartAggregate(indent_, "struct", &name, "");
    Push();

    return true;
}

bool CHeaderVisitor::EndVisit(StructDeclaration* struct_declaration) {
    Pop();
    EndAggregate(indent_);

    return true;
}

bool CHeaderVisitor::StartVisit(UnionMember* union_field) {
    PrintAggregateField(indent_, union_field->type.get(), union_field->identifier.get());

    return true;
}

bool CHeaderVisitor::EndVisit(UnionMember* union_field) {
    return true;
}

bool CHeaderVisitor::StartVisit(UnionDeclaration* union_declaration) {
    auto name = union_declaration->identifier->identifier;
    StartAggregate(indent_, "struct", &name, "");
    Push();

    printf("%*.suint32_t tag;\n", indent_, "");

    StartAggregate(indent_, "union", nullptr, "");
    Push();

    return true;
}

bool CHeaderVisitor::EndVisit(UnionDeclaration* union_declaration) {
    Pop();
    EndAggregate(indent_);
    Pop();
    EndAggregate(indent_);

    return true;
}

bool CHeaderVisitor::StartVisit(File* file) {
    printf("#pragma once\n"
           "\n"
           "#if defined(__cplusplus)\n"
           "extern \"C\" {\n"
           "#endif\n"
           "\n");

    return true;
}

bool CHeaderVisitor::EndVisit(File* file) {
    printf("#if defined(__cplusplus)\n"
           "}\n"
           "#endif\n"
           "\n");

    return true;
}

} // namespace fidl
