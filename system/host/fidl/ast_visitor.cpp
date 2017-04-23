// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ast_visitor.h"

#include <assert.h>

namespace fidl {

#define TRY_TO(do_it)     \
    do {                  \
        if (!do_it) {     \
            return false; \
        }                 \
    } while (0)

bool Visitor::Traverse(Identifier* identifier) {
    TRY_TO(StartVisit(identifier));
    TRY_TO(EndVisit(identifier));

    return true;
}

bool Visitor::Traverse(CompoundIdentifier* compound_identifier) {
    TRY_TO(StartVisit(compound_identifier));

    for (const auto& component : compound_identifier->components)
        TRY_TO(Traverse(component.get()));

    TRY_TO(EndVisit(compound_identifier));

    return true;
}

bool Visitor::Traverse(Literal* literal) {
    TRY_TO(StartVisit(literal));

    if (StringLiteral* string_literal = try_cast<StringLiteral>(literal))
        TRY_TO(Traverse(string_literal));
    else if (NumericLiteral* numeric_literal = try_cast<NumericLiteral>(literal))
        TRY_TO(Traverse(numeric_literal));
    else if (TrueLiteral* true_literal = try_cast<TrueLiteral>(literal))
        TRY_TO(Traverse(true_literal));
    else if (FalseLiteral* false_literal = try_cast<FalseLiteral>(literal))
        TRY_TO(Traverse(false_literal));
    else if (DefaultLiteral* default_literal = try_cast<DefaultLiteral>(literal))
        TRY_TO(Traverse(default_literal));
    else
        assert(false);

    TRY_TO(EndVisit(literal));

    return true;
}

bool Visitor::Traverse(StringLiteral* string_literal) {
    TRY_TO(StartVisit(string_literal));
    TRY_TO(EndVisit(string_literal));

    return true;
}

bool Visitor::Traverse(NumericLiteral* numeric_literal) {
    TRY_TO(StartVisit(numeric_literal));
    TRY_TO(EndVisit(numeric_literal));

    return true;
}

bool Visitor::Traverse(TrueLiteral* true_literal) {
    TRY_TO(StartVisit(true_literal));
    TRY_TO(EndVisit(true_literal));

    return true;
}

bool Visitor::Traverse(FalseLiteral* false_literal) {
    TRY_TO(StartVisit(false_literal));
    TRY_TO(EndVisit(false_literal));

    return true;
}

bool Visitor::Traverse(DefaultLiteral* default_literal) {
    TRY_TO(StartVisit(default_literal));
    TRY_TO(EndVisit(default_literal));

    return true;
}

bool Visitor::Traverse(Type* type) {
    TRY_TO(StartVisit(type));

    if (HandleType* handle_type = try_cast<HandleType>(type))
        TRY_TO(Traverse(handle_type));
    else if (IdentifierType* identifier_type = try_cast<IdentifierType>(type))
        TRY_TO(Traverse(identifier_type));
    else if (PrimitiveType* primitive_type = try_cast<PrimitiveType>(type))
        TRY_TO(Traverse(primitive_type));
    else if (RequestType* request_type = try_cast<RequestType>(type))
        TRY_TO(Traverse(request_type));
    else
        assert(false);

    TRY_TO(EndVisit(type));

    return true;
}

bool Visitor::Traverse(HandleType* handle_type) {
    TRY_TO(StartVisit(handle_type));

    if (handle_type->maybe_subtype)
        TRY_TO(Traverse(handle_type->maybe_subtype.get()));

    TRY_TO(EndVisit(handle_type));

    return true;
}

bool Visitor::Traverse(IdentifierType* identifier_type) {
    TRY_TO(StartVisit(identifier_type));

    TRY_TO(Traverse(identifier_type->identifier.get()));

    TRY_TO(EndVisit(identifier_type));

    return true;
}

bool Visitor::Traverse(PrimitiveType* primitive_type) {
    TRY_TO(StartVisit(primitive_type));
    TRY_TO(EndVisit(primitive_type));

    return true;
}

bool Visitor::Traverse(RequestType* request_type) {
    TRY_TO(StartVisit(request_type));

    TRY_TO(Traverse(request_type->subtype.get()));

    TRY_TO(EndVisit(request_type));

    return true;
}

bool Visitor::Traverse(Constant* constant) {
    TRY_TO(StartVisit(constant));

    if (IdentifierConstant* identifier_constant = try_cast<IdentifierConstant>(constant))
        TRY_TO(Traverse(identifier_constant));
    else if (LiteralConstant* literal_constant = try_cast<LiteralConstant>(constant))
        TRY_TO(Traverse(literal_constant));
    else
        assert(false);

    TRY_TO(EndVisit(constant));

    return true;
}

bool Visitor::Traverse(IdentifierConstant* identifier_constant) {
    TRY_TO(StartVisit(identifier_constant));

    TRY_TO(Traverse(identifier_constant->identifier.get()));

    TRY_TO(EndVisit(identifier_constant));

    return true;
}

bool Visitor::Traverse(LiteralConstant* literal_constant) {
    TRY_TO(StartVisit(literal_constant));

    TRY_TO(Traverse(literal_constant->literal.get()));

    TRY_TO(EndVisit(literal_constant));

    return true;
}

bool Visitor::Traverse(Module* module) {
    TRY_TO(StartVisit(module));

    TRY_TO(Traverse(module->identifier.get()));

    TRY_TO(EndVisit(module));

    return true;
}

bool Visitor::Traverse(Using* import) {
    TRY_TO(StartVisit(import));

    TRY_TO(Traverse(import->import_path.get()));

    TRY_TO(EndVisit(import));

    return true;
}

bool Visitor::Traverse(UsingList* import_list) {
    TRY_TO(StartVisit(import_list));

    for (const auto& import : import_list->import_list)
        TRY_TO(Traverse(import.get()));

    TRY_TO(EndVisit(import_list));

    return true;
}

bool Visitor::Traverse(Declaration* declaration) {
    TRY_TO(StartVisit(declaration));

    if (ConstDeclaration* const_declaration = try_cast<ConstDeclaration>(declaration))
        TRY_TO(Traverse(const_declaration));
    else if (EnumDeclaration* enum_declaration = try_cast<EnumDeclaration>(declaration))
        TRY_TO(Traverse(enum_declaration));
    else if (InterfaceDeclaration* interface_declaration = try_cast<InterfaceDeclaration>(declaration))
        TRY_TO(Traverse(interface_declaration));
    else if (StructDeclaration* struct_declaration = try_cast<StructDeclaration>(declaration))
        TRY_TO(Traverse(struct_declaration));
    else if (UnionDeclaration* union_declaration = try_cast<UnionDeclaration>(declaration))
        TRY_TO(Traverse(union_declaration));
    else
        assert(false);

    TRY_TO(EndVisit(declaration));

    return true;
}

bool Visitor::Traverse(ConstDeclaration* const_declaration) {
    TRY_TO(StartVisit(const_declaration));

    TRY_TO(Traverse(const_declaration->type.get()));
    TRY_TO(Traverse(const_declaration->identifier.get()));
    TRY_TO(Traverse(const_declaration->constant.get()));

    TRY_TO(EndVisit(const_declaration));

    return true;
}

bool Visitor::Traverse(EnumMemberValue* enum_field_value) {
    TRY_TO(StartVisit(enum_field_value));

    if (EnumMemberValueIdentifier* enum_field_value_identifier = try_cast<EnumMemberValueIdentifier>(enum_field_value))
        TRY_TO(Traverse(enum_field_value_identifier));
    else if (EnumMemberValueNumeric* enum_field_value_numeric = try_cast<EnumMemberValueNumeric>(enum_field_value))
        TRY_TO(Traverse(enum_field_value_numeric));
    else
        assert(false);

    TRY_TO(EndVisit(enum_field_value));

    return true;
}

bool Visitor::Traverse(EnumMemberValueIdentifier* enum_field_value_identifier) {
    TRY_TO(StartVisit(enum_field_value_identifier));

    TRY_TO(Traverse(enum_field_value_identifier->identifier.get()));

    TRY_TO(EndVisit(enum_field_value_identifier));

    return true;
}

bool Visitor::Traverse(EnumMemberValueNumeric* enum_field_value_numeric) {
    TRY_TO(StartVisit(enum_field_value_numeric));

    TRY_TO(Traverse(enum_field_value_numeric->literal.get()));

    TRY_TO(EndVisit(enum_field_value_numeric));

    return true;
}

bool Visitor::Traverse(EnumMember* enum_field) {
    TRY_TO(StartVisit(enum_field));

    TRY_TO(Traverse(enum_field->identifier.get()));
    if (enum_field->maybe_value)
        TRY_TO(Traverse(enum_field->maybe_value.get()));

    TRY_TO(EndVisit(enum_field));

    return true;
}

bool Visitor::Traverse(EnumBody* enum_body) {
    TRY_TO(StartVisit(enum_body));

    for (const auto& field : enum_body->fields)
        TRY_TO(Traverse(field.get()));

    TRY_TO(EndVisit(enum_body));

    return true;
}

bool Visitor::Traverse(EnumDeclaration* enum_declaration) {
    TRY_TO(StartVisit(enum_declaration));

    TRY_TO(Traverse(enum_declaration->identifier.get()));
    if (enum_declaration->maybe_subtype)
        TRY_TO(Traverse(enum_declaration->maybe_subtype.get()));
    TRY_TO(Traverse(enum_declaration->body.get()));

    TRY_TO(EndVisit(enum_declaration));

    return true;
}

bool Visitor::Traverse(InterfaceMember* interface_field) {
    TRY_TO(StartVisit(interface_field));

    if (InterfaceMemberConst* interface_field_const = try_cast<InterfaceMemberConst>(interface_field))
        TRY_TO(Traverse(interface_field_const));
    else if (InterfaceMemberEnum* interface_field_enum = try_cast<InterfaceMemberEnum>(interface_field))
        TRY_TO(Traverse(interface_field_enum));
    else if (InterfaceMemberMethod* interface_field_method = try_cast<InterfaceMemberMethod>(interface_field))
        TRY_TO(Traverse(interface_field_method));
    else
        assert(false);

    TRY_TO(EndVisit(interface_field));

    return true;
}

bool Visitor::Traverse(InterfaceMemberConst* interface_field_const) {
    TRY_TO(StartVisit(interface_field_const));

    TRY_TO(Traverse(interface_field_const->const_declaration.get()));

    TRY_TO(EndVisit(interface_field_const));

    return true;
}

bool Visitor::Traverse(InterfaceMemberEnum* interface_field_enum) {
    TRY_TO(StartVisit(interface_field_enum));

    TRY_TO(Traverse(interface_field_enum->enum_declaration.get()));

    TRY_TO(EndVisit(interface_field_enum));

    return true;
}

bool Visitor::Traverse(Parameter* parameter) {
    TRY_TO(StartVisit(parameter));

    TRY_TO(Traverse(parameter->type.get()));
    TRY_TO(Traverse(parameter->identifier.get()));

    TRY_TO(EndVisit(parameter));

    return true;
}

bool Visitor::Traverse(ParameterList* parameter_list) {
    TRY_TO(StartVisit(parameter_list));

    for (const auto& parameter : parameter_list->parameter_list)
        TRY_TO(Traverse(parameter.get()));

    TRY_TO(EndVisit(parameter_list));

    return true;
}

bool Visitor::Traverse(Response* response) {
    TRY_TO(StartVisit(response));

    TRY_TO(Traverse(response->parameter_list.get()));

    TRY_TO(EndVisit(response));

    return true;
}

bool Visitor::Traverse(InterfaceMemberMethod* interface_field_method) {
    TRY_TO(StartVisit(interface_field_method));

    TRY_TO(Traverse(interface_field_method->ordinal.get()));
    TRY_TO(Traverse(interface_field_method->identifier.get()));
    TRY_TO(Traverse(interface_field_method->parameter_list.get()));
    if (interface_field_method->maybe_response)
        TRY_TO(Traverse(interface_field_method->maybe_response.get()));

    TRY_TO(EndVisit(interface_field_method));

    return true;
}

bool Visitor::Traverse(InterfaceBody* interface_body) {
    TRY_TO(StartVisit(interface_body));

    for (const auto& field : interface_body->fields)
        TRY_TO(Traverse(field.get()));

    TRY_TO(EndVisit(interface_body));

    return true;
}

bool Visitor::Traverse(InterfaceDeclaration* interface_declaration) {
    TRY_TO(StartVisit(interface_declaration));

    TRY_TO(Traverse(interface_declaration->identifier.get()));
    TRY_TO(Traverse(interface_declaration->body.get()));

    TRY_TO(EndVisit(interface_declaration));

    return true;
}

bool Visitor::Traverse(StructMember* struct_field) {
    TRY_TO(StartVisit(struct_field));

    if (StructMemberConst* struct_field_const = try_cast<StructMemberConst>(struct_field))
        TRY_TO(Traverse(struct_field_const));
    else if (StructMemberEnum* struct_field_enum = try_cast<StructMemberEnum>(struct_field))
        TRY_TO(Traverse(struct_field_enum));
    else if (StructMemberField* struct_field_field = try_cast<StructMemberField>(struct_field))
        TRY_TO(Traverse(struct_field_field));
    else
        assert(false);

    TRY_TO(EndVisit(struct_field));

    return true;
}

bool Visitor::Traverse(StructMemberConst* struct_field_const) {
    TRY_TO(StartVisit(struct_field_const));

    TRY_TO(Traverse(struct_field_const->const_declaration.get()));

    TRY_TO(EndVisit(struct_field_const));

    return true;
}

bool Visitor::Traverse(StructMemberEnum* struct_field_enum) {
    TRY_TO(StartVisit(struct_field_enum));

    TRY_TO(Traverse(struct_field_enum->enum_declaration.get()));

    TRY_TO(EndVisit(struct_field_enum));

    return true;
}

bool Visitor::Traverse(StructDefaultValue* struct_default_value) {
    TRY_TO(StartVisit(struct_default_value));

    TRY_TO(Traverse(struct_default_value->const_declaration.get()));

    TRY_TO(EndVisit(struct_default_value));

    return true;
}

bool Visitor::Traverse(StructMemberField* struct_field_field) {
    TRY_TO(StartVisit(struct_field_field));

    TRY_TO(Traverse(struct_field_field->type.get()));
    TRY_TO(Traverse(struct_field_field->identifier.get()));
    if (struct_field_field->maybe_default_value)
        TRY_TO(Traverse(struct_field_field->maybe_default_value.get()));

    TRY_TO(EndVisit(struct_field_field));

    return true;
}

bool Visitor::Traverse(StructBody* struct_body) {
    TRY_TO(StartVisit(struct_body));

    for (const auto& field : struct_body->fields)
        TRY_TO(Traverse(field.get()));

    TRY_TO(EndVisit(struct_body));

    return true;
}

bool Visitor::Traverse(StructDeclaration* struct_declaration) {
    TRY_TO(StartVisit(struct_declaration));

    TRY_TO(Traverse(struct_declaration->identifier.get()));
    TRY_TO(Traverse(struct_declaration->body.get()));

    TRY_TO(EndVisit(struct_declaration));

    return true;
}

bool Visitor::Traverse(UnionMember* union_field) {
    TRY_TO(EndVisit(union_field));

    TRY_TO(Traverse(union_field->type.get()));
    TRY_TO(Traverse(union_field->identifier.get()));

    TRY_TO(StartVisit(union_field));

    return true;
}

bool Visitor::Traverse(UnionBody* union_body) {
    TRY_TO(StartVisit(union_body));

    for (const auto& field : union_body->fields)
        TRY_TO(Traverse(field.get()));

    TRY_TO(EndVisit(union_body));

    return true;
}

bool Visitor::Traverse(UnionDeclaration* union_declaration) {
    TRY_TO(StartVisit(union_declaration));

    TRY_TO(Traverse(union_declaration->identifier.get()));
    TRY_TO(Traverse(union_declaration->body.get()));

    TRY_TO(EndVisit(union_declaration));

    return true;
}

bool Visitor::Traverse(DeclarationList* declaration_list) {
    TRY_TO(StartVisit(declaration_list));

    for (const auto& declaration : declaration_list->declaration_list)
        TRY_TO(Traverse(declaration.get()));

    TRY_TO(EndVisit(declaration_list));

    return true;
}

bool Visitor::Traverse(File* file) {
    TRY_TO(StartVisit(file));

    if (file->maybe_module)
        TRY_TO(Traverse(file->maybe_module.get()));
    TRY_TO(Traverse(file->import_list.get()));
    TRY_TO(Traverse(file->declaration_list.get()));

    TRY_TO(EndVisit(file));

    return true;
}

} // namespace fidl
