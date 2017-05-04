// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ast.h"

namespace fidl {

struct Visitor {
    virtual bool StartVisit(Identifier* identifier) { return true; }
    virtual bool EndVisit(Identifier* identifier) { return true; }
    virtual bool StartVisit(CompoundIdentifier* compound_identifier) { return true; }
    virtual bool EndVisit(CompoundIdentifier* compound_identifier) { return true; }
    virtual bool StartVisit(Literal* literal) { return true; }
    virtual bool EndVisit(Literal* literal) { return true; }
    virtual bool StartVisit(StringLiteral* string_literal) { return true; }
    virtual bool EndVisit(StringLiteral* string_literal) { return true; }
    virtual bool StartVisit(NumericLiteral* numeric_literal) { return true; }
    virtual bool EndVisit(NumericLiteral* numeric_literal) { return true; }
    virtual bool StartVisit(TrueLiteral* numeric_literal) { return true; }
    virtual bool EndVisit(TrueLiteral* numeric_literal) { return true; }
    virtual bool StartVisit(FalseLiteral* numeric_literal) { return true; }
    virtual bool EndVisit(FalseLiteral* numeric_literal) { return true; }
    virtual bool StartVisit(DefaultLiteral* default_literal) { return true; }
    virtual bool EndVisit(DefaultLiteral* default_literal) { return true; }
    virtual bool StartVisit(Type* type) { return true; }
    virtual bool EndVisit(Type* type) { return true; }
    virtual bool StartVisit(HandleType* handle_type) { return true; }
    virtual bool EndVisit(HandleType* handle_type) { return true; }
    virtual bool StartVisit(IdentifierType* identifier_type) { return true; }
    virtual bool EndVisit(IdentifierType* identifier_type) { return true; }
    virtual bool StartVisit(PrimitiveType* primitive_type) { return true; }
    virtual bool EndVisit(PrimitiveType* primitive_type) { return true; }
    virtual bool StartVisit(RequestType* request_type) { return true; }
    virtual bool EndVisit(RequestType* request_type) { return true; }
    virtual bool StartVisit(Constant* constant) { return true; }
    virtual bool EndVisit(Constant* constant) { return true; }
    virtual bool StartVisit(IdentifierConstant* identifier_constant) { return true; }
    virtual bool EndVisit(IdentifierConstant* identifier_constant) { return true; }
    virtual bool StartVisit(LiteralConstant* literal_constant) { return true; }
    virtual bool EndVisit(LiteralConstant* literal_constant) { return true; }
    virtual bool StartVisit(ModuleName* module_name) { return true; }
    virtual bool EndVisit(ModuleName* module_name) { return true; }
    virtual bool StartVisit(Using* import) { return true; }
    virtual bool EndVisit(Using* import) { return true; }
    virtual bool StartVisit(UsingList* import_list) { return true; }
    virtual bool EndVisit(UsingList* import_list) { return true; }
    virtual bool StartVisit(Declaration* declaration) { return true; }
    virtual bool EndVisit(Declaration* declaration) { return true; }
    virtual bool StartVisit(ConstDeclaration* const_declaration) { return true; }
    virtual bool EndVisit(ConstDeclaration* const_declaration) { return true; }
    virtual bool StartVisit(EnumMemberValue* enum_field_value) { return true; }
    virtual bool EndVisit(EnumMemberValue* enum_field_value) { return true; }
    virtual bool StartVisit(EnumMemberValueIdentifier* enum_field_value_identifier) { return true; }
    virtual bool EndVisit(EnumMemberValueIdentifier* enum_field_value_identifier) { return true; }
    virtual bool StartVisit(EnumMemberValueNumeric* enum_field_value_numeric) { return true; }
    virtual bool EndVisit(EnumMemberValueNumeric* enum_field_value_numeric) { return true; }
    virtual bool StartVisit(EnumMember* enum_field) { return true; }
    virtual bool EndVisit(EnumMember* enum_field) { return true; }
    virtual bool StartVisit(EnumBody* enum_body) { return true; }
    virtual bool EndVisit(EnumBody* enum_body) { return true; }
    virtual bool StartVisit(EnumDeclaration* enum_declaration) { return true; }
    virtual bool EndVisit(EnumDeclaration* enum_declaration) { return true; }
    virtual bool StartVisit(InterfaceMember* interface_field) { return true; }
    virtual bool EndVisit(InterfaceMember* interface_field) { return true; }
    virtual bool StartVisit(InterfaceMemberConst* interface_field_const) { return true; }
    virtual bool EndVisit(InterfaceMemberConst* interface_field_const) { return true; }
    virtual bool StartVisit(InterfaceMemberEnum* interface_field_enum) { return true; }
    virtual bool EndVisit(InterfaceMemberEnum* interface_field_enum) { return true; }
    virtual bool StartVisit(Parameter* parameter) { return true; }
    virtual bool EndVisit(Parameter* parameter) { return true; }
    virtual bool StartVisit(ParameterList* parameter_list) { return true; }
    virtual bool EndVisit(ParameterList* parameter_list) { return true; }
    virtual bool StartVisit(Response* response) { return true; }
    virtual bool EndVisit(Response* response) { return true; }
    virtual bool StartVisit(InterfaceMemberMethod* interface_field_method) { return true; }
    virtual bool EndVisit(InterfaceMemberMethod* interface_field_method) { return true; }
    virtual bool StartVisit(InterfaceBody* interface_body) { return true; }
    virtual bool EndVisit(InterfaceBody* interface_body) { return true; }
    virtual bool StartVisit(InterfaceDeclaration* interface_declaration) { return true; }
    virtual bool EndVisit(InterfaceDeclaration* interface_declaration) { return true; }
    virtual bool StartVisit(StructMember* struct_field) { return true; }
    virtual bool EndVisit(StructMember* struct_field) { return true; }
    virtual bool StartVisit(StructMemberConst* struct_field_const) { return true; }
    virtual bool EndVisit(StructMemberConst* struct_field_const) { return true; }
    virtual bool StartVisit(StructMemberEnum* struct_field_enum) { return true; }
    virtual bool EndVisit(StructMemberEnum* struct_field_enum) { return true; }
    virtual bool StartVisit(StructDefaultValue* struct_default_value) { return true; }
    virtual bool EndVisit(StructDefaultValue* struct_default_value) { return true; }
    virtual bool StartVisit(StructMemberField* struct_field_field) { return true; }
    virtual bool EndVisit(StructMemberField* struct_field_field) { return true; }
    virtual bool StartVisit(StructBody* struct_body) { return true; }
    virtual bool EndVisit(StructBody* struct_body) { return true; }
    virtual bool StartVisit(StructDeclaration* struct_declaration) { return true; }
    virtual bool EndVisit(StructDeclaration* struct_declaration) { return true; }
    virtual bool StartVisit(UnionMember* union_field) { return true; }
    virtual bool EndVisit(UnionMember* union_field) { return true; }
    virtual bool StartVisit(UnionBody* union_body) { return true; }
    virtual bool EndVisit(UnionBody* union_body) { return true; }
    virtual bool StartVisit(UnionDeclaration* union_declaration) { return true; }
    virtual bool EndVisit(UnionDeclaration* union_declaration) { return true; }
    virtual bool StartVisit(DeclarationList* declaration_list) { return true; }
    virtual bool EndVisit(DeclarationList* declaration_list) { return true; }
    virtual bool StartVisit(File* file) { return true; }
    virtual bool EndVisit(File* file) { return true; }

    virtual bool Traverse(Identifier* identifier);
    virtual bool Traverse(CompoundIdentifier* compound_identifier);
    virtual bool Traverse(Literal* literal);
    virtual bool Traverse(StringLiteral* string_literal);
    virtual bool Traverse(NumericLiteral* numeric_literal);
    virtual bool Traverse(TrueLiteral* numeric_literal);
    virtual bool Traverse(FalseLiteral* numeric_literal);
    virtual bool Traverse(DefaultLiteral* default_literal);
    virtual bool Traverse(Type* type);
    virtual bool Traverse(HandleType* handle_type);
    virtual bool Traverse(IdentifierType* identifier_type);
    virtual bool Traverse(PrimitiveType* primitive_type);
    virtual bool Traverse(RequestType* request_type);
    virtual bool Traverse(Constant* constant);
    virtual bool Traverse(IdentifierConstant* identifier_constant);
    virtual bool Traverse(LiteralConstant* literal_constant);
    virtual bool Traverse(ModuleName* module_name);
    virtual bool Traverse(Using* import);
    virtual bool Traverse(UsingList* import_list);
    virtual bool Traverse(Declaration* declaration);
    virtual bool Traverse(ConstDeclaration* const_declaration);
    virtual bool Traverse(EnumMemberValue* enum_field_value);
    virtual bool Traverse(EnumMemberValueIdentifier* enum_field_value_identifier);
    virtual bool Traverse(EnumMemberValueNumeric* enum_field_value_numeric);
    virtual bool Traverse(EnumMember* enum_field);
    virtual bool Traverse(EnumBody* enum_body);
    virtual bool Traverse(EnumDeclaration* enum_declaration);
    virtual bool Traverse(InterfaceMember* interface_field);
    virtual bool Traverse(InterfaceMemberConst* interface_field_const);
    virtual bool Traverse(InterfaceMemberEnum* interface_field_enum);
    virtual bool Traverse(Parameter* parameter);
    virtual bool Traverse(ParameterList* parameter_list);
    virtual bool Traverse(Response* response);
    virtual bool Traverse(InterfaceMemberMethod* interface_field_method);
    virtual bool Traverse(InterfaceBody* interface_body);
    virtual bool Traverse(InterfaceDeclaration* interface_declaration);
    virtual bool Traverse(StructMember* struct_field);
    virtual bool Traverse(StructMemberConst* struct_field_const);
    virtual bool Traverse(StructMemberEnum* struct_field_enum);
    virtual bool Traverse(StructDefaultValue* struct_default_value);
    virtual bool Traverse(StructMemberField* struct_field_field);
    virtual bool Traverse(StructBody* struct_body);
    virtual bool Traverse(StructDeclaration* struct_declaration);
    virtual bool Traverse(UnionMember* union_field);
    virtual bool Traverse(UnionBody* union_body);
    virtual bool Traverse(UnionDeclaration* union_declaration);
    virtual bool Traverse(DeclarationList* declaration_list);
    virtual bool Traverse(File* file);
};

} // namespace fidl
