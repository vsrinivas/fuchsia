// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_NAMES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_NAMES_H_

#include <string>

#include "c_generator.h"
#include "flat_ast.h"
#include "raw_ast.h"
#include "string_view.h"
#include "types.h"

namespace fidl {

std::string StringJoin(const std::vector<StringView>& strings, StringView separator);

std::string NameIdentifier(SourceLocation name);
std::string NameName(const flat::Name& name, StringView library_separator, StringView separator);

std::string NameLibrary(const std::vector<StringView>& library_name);
std::string NameLibraryCHeader(const std::vector<StringView>& library_name);

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype);
std::string NamePrimitiveSubtype(types::PrimitiveSubtype subtype);
std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype type);
std::string NameHandleSubtype(types::HandleSubtype subtype);
std::string NameHandleZXObjType(types::HandleSubtype subtype);

std::string NameRawLiteralKind(raw::Literal::Kind kind);

std::string NameFlatConstantKind(flat::Constant::Kind kind);
std::string NameFlatTypeKind(flat::Type::Kind kind);
std::string NameUnionTag(StringView union_name, const flat::Union::Member& member);
std::string NameFlatCType(const flat::Library* library, const flat::Type* type);
std::string NameInterface(const flat::Interface& interface);
std::string NameDiscoverable(const flat::Interface& interface);
std::string NameMethod(StringView interface_name, const flat::Interface::Method& method);
std::string NameOrdinal(StringView method_name);
std::string NameMessage(StringView method_name, types::MessageKind kind);
std::string NameParameter(StringView message_name,
                          const flat::Interface::Method::Parameter& parameter);

std::string NameTable(StringView type_name);
std::string NamePointer(StringView name);
std::string NameMembers(StringView name);
std::string NameFields(StringView name);

std::string NameCodedStruct(const flat::Struct* struct_decl);
std::string NameCodedUnion(const flat::Union* union_decl);
std::string NameCodedHandle(types::HandleSubtype subtype, types::Nullability nullability);
std::string NameCodedInterfaceHandle(StringView interface_name, types::Nullability nullability);
std::string NameCodedRequestHandle(StringView interface_name, types::Nullability nullability);
std::string NameCodedArray(StringView element_name, uint64_t size);
std::string NameCodedVector(StringView element_name, uint64_t max_size,
                            types::Nullability nullability);
std::string NameCodedString(uint64_t max_size, types::Nullability nullability);

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_NAMES_H_
