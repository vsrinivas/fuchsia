// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_NAMES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_NAMES_H_

#include <string>
#include <string_view>

#include "c_generator.h"
#include "flat_ast.h"
#include "raw_ast.h"
#include "types.h"

namespace fidl {

std::string StringJoin(const std::vector<std::string_view>& strings, std::string_view separator);

std::string NameIdentifier(SourceSpan name);

std::string NameLibrary(const std::vector<std::unique_ptr<raw::Identifier>>& components);
std::string NameLibrary(const std::vector<std::string_view>& library_name);
std::string NameLibraryCHeader(const std::vector<std::string_view>& library_name);

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype);
std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype type);
std::string NameHandleSubtype(types::HandleSubtype subtype);
std::string NameHandleZXObjType(types::HandleSubtype subtype);

std::string NameRawLiteralKind(raw::Literal::Kind kind);

std::string NameFlatName(const flat::Name& name);
std::string NameFlatConstantKind(flat::Constant::Kind kind);
std::string NameFlatTypeKind(flat::Type::Kind kind);
std::string NameUnionTag(std::string_view union_name, const flat::Union::Member::Used& member);
std::string NameXUnionTag(std::string_view xunion_name, const flat::XUnion::Member::Used& member);
std::string NameFlatConstant(const flat::Constant* constant);
std::string NameFlatBinaryOperator(flat::BinaryOperatorConstant::Operator op);
std::string NameFlatConstant(const flat::Constant* constant);
std::string NameFlatTypeConstructor(const flat::TypeConstructor* type_ctor);
std::string NameFlatType(const flat::Type* type);
std::string NameFlatCType(const flat::Type* type, flat::Decl::Kind decl_kind,
                          const WireFormat wire_format);
std::string NameDiscoverable(const flat::Protocol& protocol);
std::string NameMethod(std::string_view protocol_name, const flat::Protocol::Method& method);
std::string NameOrdinal(std::string_view method_name);
std::string NameGenOrdinal(std::string_view method_name);
std::string NameMessage(std::string_view method_name, types::MessageKind kind);

std::string NameTable(std::string_view table_name);
std::string NamePointer(std::string_view name, const WireFormat wire_format);
std::string NameMembers(std::string_view name);
std::string NameFields(std::string_view name);
std::string NameFieldsAltField(std::string_view name, uint32_t field_num);

std::string NameCodedName(const flat::Name& name, const WireFormat wire_format);
std::string NameCodedNullableName(const flat::Name& name, const WireFormat wire_format);
std::string NameCodedHandle(types::HandleSubtype subtype, types::Nullability nullability,
                            const WireFormat wire_format);
std::string NameCodedProtocolHandle(std::string_view protocol_name, types::Nullability nullability,
                                    const WireFormat wire_format);
std::string NameCodedRequestHandle(std::string_view protocol_name, types::Nullability nullability,
                                   const WireFormat wire_format);
std::string NameCodedArray(std::string_view element_name, uint64_t size,
                           const WireFormat wire_format);
std::string NameCodedVector(std::string_view element_name, uint64_t max_size,
                            types::Nullability nullability, const WireFormat wire_format);
std::string NameCodedString(uint64_t max_size, types::Nullability nullability,
                            const WireFormat wire_format);

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_NAMES_H_
