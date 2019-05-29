// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_NAMES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_NAMES_H_

#include <string>
#include <string_view>

#include "c_generator.h"
#include "flat_ast.h"
#include "raw_ast.h"
#include "types.h"

namespace fidl {

std::string StringJoin(const std::vector<std::string_view>& strings, std::string_view separator);

std::string NameIdentifier(SourceLocation name);
std::string NameName(const flat::Name& name, std::string_view library_separator, std::string_view separator);

std::string NameLibrary(const std::vector<std::unique_ptr<raw::Identifier>>& components);
std::string NameLibrary(const std::vector<std::string_view>& library_name);
std::string NameLibraryCHeader(const std::vector<std::string_view>& library_name);

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype);
std::string NamePrimitiveSubtype(types::PrimitiveSubtype subtype);
std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype type);
std::string NameHandleSubtype(types::HandleSubtype subtype);
std::string NameHandleZXObjType(types::HandleSubtype subtype);

std::string NameRawLiteralKind(raw::Literal::Kind kind);

std::string NameFlatConstantKind(flat::Constant::Kind kind);
std::string NameFlatTypeKind(flat::Type::Kind kind);
std::string NameUnionTag(std::string_view union_name, const flat::Union::Member& member);
std::string NameXUnionTag(std::string_view xunion_name, const flat::XUnion::Member& member);
std::string NameFlatConstant(const flat::Constant* constant);
std::string NameFlatTypeConstructor(const flat::TypeConstructor* type_ctor);
std::string NameFlatType(const flat::Type* type);
std::string NameFlatCType(const flat::Type* type, flat::Decl::Kind decl_kind);
std::string NameInterface(const flat::Interface& interface);
std::string NameDiscoverable(const flat::Interface& interface);
std::string NameMethod(std::string_view interface_name, const flat::Interface::Method& method);
std::string NameOrdinal(std::string_view method_name);
std::string NameGenOrdinal(std::string_view method_name);
std::string NameMessage(std::string_view method_name, types::MessageKind kind);

std::string NameTable(std::string_view type_name);
std::string NamePointer(std::string_view name);
std::string NameMembers(std::string_view name);
std::string NameFields(std::string_view name);

std::string NameCodedStruct(const flat::Struct* struct_decl);
std::string NameCodedTable(const flat::Table* table_decl);
std::string NameCodedUnion(const flat::Union* union_decl);
std::string NameCodedXUnion(const flat::XUnion* xunion_decl);
std::string NameCodedHandle(types::HandleSubtype subtype, types::Nullability nullability);
std::string NameCodedInterfaceHandle(std::string_view interface_name, types::Nullability nullability);
std::string NameCodedRequestHandle(std::string_view interface_name, types::Nullability nullability);
std::string NameCodedArray(std::string_view element_name, uint64_t size);
std::string NameCodedVector(std::string_view element_name, uint64_t max_size,
                            types::Nullability nullability);
std::string NameCodedString(uint64_t max_size, types::Nullability nullability);

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_NAMES_H_
