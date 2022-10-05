// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/names.h"

#include <zircon/assert.h>

namespace fidl {

namespace {

const char* NameNullability(types::Nullability nullability) {
  switch (nullability) {
    case types::Nullability::kNullable:
      return "nullable";
    case types::Nullability::kNonnullable:
      return "nonnullable";
  }
}

std::string NameSize(uint64_t size) {
  if (size == std::numeric_limits<uint64_t>::max())
    return "unbounded";
  std::ostringstream name;
  name << size;
  return name.str();
}

std::string FormatName(const flat::Name& name, std::string_view library_separator,
                       std::string_view name_separator) {
  std::string compiled_name;
  if (name.library() != nullptr && !name.is_intrinsic()) {
    compiled_name += flat::LibraryName(name.library()->name, library_separator);
    compiled_name += name_separator;
  }
  compiled_name += name.full_name();
  return compiled_name;
}

std::string LengthPrefixedString(std::string_view str) {
  std::ostringstream out;
  out << str.length();
  out << str;
  return out.str();
}

}  // namespace

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype) {
  switch (subtype) {
    case types::PrimitiveSubtype::kInt8:
      return "int8_t";
    case types::PrimitiveSubtype::kInt16:
      return "int16_t";
    case types::PrimitiveSubtype::kInt32:
      return "int32_t";
    case types::PrimitiveSubtype::kInt64:
      return "int64_t";
    case types::PrimitiveSubtype::kUint8:
      return "uint8_t";
    case types::PrimitiveSubtype::kZxUchar:
      return "unsigned char";
    case types::PrimitiveSubtype::kUint16:
      return "uint16_t";
    case types::PrimitiveSubtype::kUint32:
      return "uint32_t";
    case types::PrimitiveSubtype::kUint64:
      return "uint64_t";
    case types::PrimitiveSubtype::kZxUsize:
      return "size_t";
    case types::PrimitiveSubtype::kZxUintptr:
      return "uintptr_t";
    case types::PrimitiveSubtype::kBool:
      return "bool";
    case types::PrimitiveSubtype::kFloat32:
      return "float";
    case types::PrimitiveSubtype::kFloat64:
      return "double";
  }
}

std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype subtype) {
  switch (subtype) {
    case types::PrimitiveSubtype::kInt8:
      return "INT8_C";
    case types::PrimitiveSubtype::kInt16:
      return "INT16_C";
    case types::PrimitiveSubtype::kInt32:
      return "INT32_C";
    case types::PrimitiveSubtype::kInt64:
      return "INT64_C";
    case types::PrimitiveSubtype::kUint8:
    case types::PrimitiveSubtype::kZxUchar:
      return "UINT8_C";
    case types::PrimitiveSubtype::kUint16:
      return "UINT16_C";
    case types::PrimitiveSubtype::kUint32:
      return "UINT32_C";
    case types::PrimitiveSubtype::kUint64:
    case types::PrimitiveSubtype::kZxUsize:
    case types::PrimitiveSubtype::kZxUintptr:
      return "UINT64_C";
    case types::PrimitiveSubtype::kBool:
      ZX_PANIC("tried to generate an integer constant for a bool");
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      ZX_PANIC("tried to generate an integer constant for a float");
  }
}

std::string NameHandleSubtype(types::HandleSubtype subtype) {
  switch (subtype) {
    case types::HandleSubtype::kHandle:
      return "handle";
    case types::HandleSubtype::kBti:
      return "bti";
    case types::HandleSubtype::kChannel:
      return "channel";
    case types::HandleSubtype::kClock:
      return "clock";
    case types::HandleSubtype::kEvent:
      return "event";
    case types::HandleSubtype::kEventpair:
      return "eventpair";
    case types::HandleSubtype::kException:
      return "exception";
    case types::HandleSubtype::kFifo:
      return "fifo";
    case types::HandleSubtype::kGuest:
      return "guest";
    case types::HandleSubtype::kInterrupt:
      return "interrupt";
    case types::HandleSubtype::kIommu:
      return "iommu";
    case types::HandleSubtype::kJob:
      return "job";
    case types::HandleSubtype::kLog:
      return "debuglog";
    case types::HandleSubtype::kMsi:
      return "msi";
    case types::HandleSubtype::kPager:
      return "pager";
    case types::HandleSubtype::kPciDevice:
      return "pcidevice";
    case types::HandleSubtype::kPmt:
      return "pmt";
    case types::HandleSubtype::kPort:
      return "port";
    case types::HandleSubtype::kProcess:
      return "process";
    case types::HandleSubtype::kProfile:
      return "profile";
    case types::HandleSubtype::kResource:
      return "resource";
    case types::HandleSubtype::kSocket:
      return "socket";
    case types::HandleSubtype::kStream:
      return "stream";
    case types::HandleSubtype::kSuspendToken:
      return "suspendtoken";
    case types::HandleSubtype::kThread:
      return "thread";
    case types::HandleSubtype::kTimer:
      return "timer";
    case types::HandleSubtype::kVcpu:
      return "vcpu";
    case types::HandleSubtype::kVmar:
      return "vmar";
    case types::HandleSubtype::kVmo:
      return "vmo";
  }
}

std::string NameHandleRights(types::RightsWrappedType rights) { return std::to_string(rights); }

std::string NameRawLiteralKind(raw::Literal::Kind kind) {
  switch (kind) {
    case raw::Literal::Kind::kDocComment:
    case raw::Literal::Kind::kString:
      return "string";
    case raw::Literal::Kind::kNumeric:
      return "numeric";
    case raw::Literal::Kind::kBool:
      return "bool";
  }
}

std::string NameFlatName(const flat::Name& name) { return FormatName(name, ".", "/"); }

std::string NameFlatTypeKind(const flat::Type* type) {
  switch (type->kind) {
    case flat::Type::Kind::kArray:
      return "array";
    case flat::Type::Kind::kVector:
      return "vector";
    case flat::Type::Kind::kZxExperimentalPointer:
      return "experimental_pointer";
    case flat::Type::Kind::kString:
      return "string";
    case flat::Type::Kind::kHandle:
      return "handle";
    case flat::Type::Kind::kTransportSide: {
      // TODO(fxbug.dev/70186): transition the JSON and other backends to using
      // client/server end
      auto channel_end = static_cast<const flat::TransportSideType*>(type);
      return (channel_end->end == flat::TransportSide::kClient) ? "identifier" : "request";
    }
    case flat::Type::Kind::kPrimitive:
      return "primitive";
    case flat::Type::Kind::kInternal:
      return "internal";
    // TODO(fxbug.dev/70186): transition the JSON and other backends to using box
    case flat::Type::Kind::kBox:
    case flat::Type::Kind::kIdentifier:
      return "identifier";
    case flat::Type::Kind::kUntypedNumeric:
      ZX_PANIC("should not have untyped numeric here");
  }
}

std::string NameFlatConstantKind(flat::Constant::Kind kind) {
  switch (kind) {
    case flat::Constant::Kind::kIdentifier:
      return "identifier";
    case flat::Constant::Kind::kLiteral:
      return "literal";
    case flat::Constant::Kind::kBinaryOperator:
      return "binary_operator";
  }
}

std::string NameHandleZXObjType(types::HandleSubtype subtype) {
  switch (subtype) {
    case types::HandleSubtype::kHandle:
      return "ZX_OBJ_TYPE_NONE";
    case types::HandleSubtype::kBti:
      return "ZX_OBJ_TYPE_BTI";
    case types::HandleSubtype::kChannel:
      return "ZX_OBJ_TYPE_CHANNEL";
    case types::HandleSubtype::kClock:
      return "ZX_OBJ_TYPE_CLOCK";
    case types::HandleSubtype::kEvent:
      return "ZX_OBJ_TYPE_EVENT";
    case types::HandleSubtype::kEventpair:
      return "ZX_OBJ_TYPE_EVENTPAIR";
    case types::HandleSubtype::kException:
      return "ZX_OBJ_TYPE_EXCEPTION";
    case types::HandleSubtype::kFifo:
      return "ZX_OBJ_TYPE_FIFO";
    case types::HandleSubtype::kGuest:
      return "ZX_OBJ_TYPE_GUEST";
    case types::HandleSubtype::kInterrupt:
      return "ZX_OBJ_TYPE_INTERRUPT";
    case types::HandleSubtype::kIommu:
      return "ZX_OBJ_TYPE_IOMMU";
    case types::HandleSubtype::kJob:
      return "ZX_OBJ_TYPE_JOB";
    case types::HandleSubtype::kLog:
      return "ZX_OBJ_TYPE_LOG";
    case types::HandleSubtype::kMsi:
      return "ZX_OBJ_TYPE_MSI";
    case types::HandleSubtype::kPager:
      return "ZX_OBJ_TYPE_PAGER";
    case types::HandleSubtype::kPciDevice:
      return "ZX_OBJ_TYPE_PCI_DEVICE";
    case types::HandleSubtype::kPmt:
      return "ZX_OBJ_TYPE_PMT";
    case types::HandleSubtype::kPort:
      return "ZX_OBJ_TYPE_PORT";
    case types::HandleSubtype::kProcess:
      return "ZX_OBJ_TYPE_PROCESS";
    case types::HandleSubtype::kProfile:
      return "ZX_OBJ_TYPE_PROFILE";
    case types::HandleSubtype::kResource:
      return "ZX_OBJ_TYPE_RESOURCE";
    case types::HandleSubtype::kSocket:
      return "ZX_OBJ_TYPE_SOCKET";
    case types::HandleSubtype::kStream:
      return "ZX_OBJ_TYPE_STREAM";
    case types::HandleSubtype::kSuspendToken:
      return "ZX_OBJ_TYPE_SUSPEND_TOKEN";
    case types::HandleSubtype::kThread:
      return "ZX_OBJ_TYPE_THREAD";
    case types::HandleSubtype::kTimer:
      return "ZX_OBJ_TYPE_TIMER";
    case types::HandleSubtype::kVcpu:
      return "ZX_OBJ_TYPE_VCPU";
    case types::HandleSubtype::kVmar:
      return "ZX_OBJ_TYPE_VMAR";
    case types::HandleSubtype::kVmo:
      return "ZX_OBJ_TYPE_VMO";
  }
}

std::string NameUnionTag(std::string_view union_name, const flat::Union::Member::Used& member) {
  return std::string(union_name) + "Tag_" + NameIdentifier(member.name);
}

std::string NameFlatConstant(const flat::Constant* constant) {
  switch (constant->kind) {
    case flat::Constant::Kind::kLiteral: {
      auto literal_constant = static_cast<const flat::LiteralConstant*>(constant);
      return std::string(literal_constant->literal->span().data());
    }
    case flat::Constant::Kind::kIdentifier: {
      auto identifier_constant = static_cast<const flat::IdentifierConstant*>(constant);
      return NameFlatName(identifier_constant->reference.resolved().name());
    }
    case flat::Constant::Kind::kBinaryOperator: {
      return std::string("binary operator");
    }
  }  // switch
}

void NameFlatTypeHelper(std::ostringstream& buf, const flat::Type* type) {
  buf << NameFlatName(type->name);
  switch (type->kind) {
    case flat::Type::Kind::kArray: {
      const auto* array_type = static_cast<const flat::ArrayType*>(type);
      buf << "<";
      NameFlatTypeHelper(buf, array_type->element_type);
      if (*array_type->element_count != flat::Size::Max()) {
        buf << ", ";
        buf << array_type->element_count->value;
      }
      buf << ">";
      break;
    }
    case flat::Type::Kind::kVector: {
      const auto* vector_type = static_cast<const flat::VectorType*>(type);
      buf << "<";
      NameFlatTypeHelper(buf, vector_type->element_type);
      buf << ">";
      if (*vector_type->element_count != flat::Size::Max()) {
        buf << ":";
        buf << vector_type->element_count->value;
      }
      break;
    }
    case flat::Type::Kind::kString: {
      const auto* string_type = static_cast<const flat::StringType*>(type);
      if (*string_type->max_size != flat::Size::Max()) {
        buf << ":";
        buf << string_type->max_size->value;
      }
      break;
    }
    case flat::Type::Kind::kZxExperimentalPointer: {
      const auto* pointer_type = static_cast<const flat::ZxExperimentalPointerType*>(type);
      buf << "<";
      NameFlatTypeHelper(buf, pointer_type->pointee_type);
      buf << ">";
      break;
    }
    case flat::Type::Kind::kHandle: {
      const auto* handle_type = static_cast<const flat::HandleType*>(type);
      if (handle_type->subtype != types::HandleSubtype::kHandle) {
        buf << ":";
        buf << NameHandleSubtype(handle_type->subtype);
      }
      break;
    }
    case flat::Type::Kind::kTransportSide: {
      const auto* transport_side = static_cast<const flat::TransportSideType*>(type);
      buf << (transport_side->end == flat::TransportSide::kClient ? "client" : "server");
      buf << ":";
      buf << NameFlatName(transport_side->protocol_decl->name);
      break;
    }
    case flat::Type::Kind::kBox: {
      const auto* box_type = static_cast<const flat::BoxType*>(type);
      buf << "<";
      buf << NameFlatName(box_type->boxed_type->name);
      buf << ">";
      break;
    }
    case flat::Type::Kind::kPrimitive:
    case flat::Type::Kind::kInternal:
    case flat::Type::Kind::kIdentifier:
    case flat::Type::Kind::kUntypedNumeric:
      // Like Stars, they are known by name.
      break;
  }  // switch
  // TODO(fxbug.dev/93999): Use the new syntax, `:optional`.
  if (type->nullability == types::Nullability::kNullable) {
    buf << "?";
  }
}

std::string NameFlatType(const flat::Type* type) {
  std::ostringstream buf;
  NameFlatTypeHelper(buf, type);
  return buf.str();
}

std::string NameFlatCType(const flat::Type* type) {
  for (;;) {
    switch (type->kind) {
      case flat::Type::Kind::kHandle:
      case flat::Type::Kind::kTransportSide:
        return "zx_handle_t";

      case flat::Type::Kind::kVector:
        return "fidl_vector_t";
      case flat::Type::Kind::kString:
        return "fidl_string_t";

      case flat::Type::Kind::kPrimitive: {
        auto primitive_type = static_cast<const flat::PrimitiveType*>(type);
        return NamePrimitiveCType(primitive_type->subtype);
      }
      case flat::Type::Kind::kInternal:
        ZX_PANIC("C bindings should not be using unknown interactinos");

      case flat::Type::Kind::kArray: {
        type = static_cast<const flat::ArrayType*>(type)->element_type;
        continue;
      }

      case flat::Type::Kind::kBox: {
        type = static_cast<const flat::BoxType*>(type)->boxed_type;
        continue;
      }

      case flat::Type::Kind::kIdentifier: {
        auto identifier_type = static_cast<const flat::IdentifierType*>(type);
        switch (identifier_type->type_decl->kind) {
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kStruct: {
            std::string name = NameCodedName(identifier_type->name);
            if (identifier_type->nullability == types::Nullability::kNullable) {
              name.push_back('*');
            }
            return name;
          }
          case flat::Decl::Kind::kUnion:
            return "fidl_xunion_t";
          case flat::Decl::Kind::kTable:
            return "fidl_table_t";
          case flat::Decl::Kind::kProtocol:
            return "zx_handle_t";
          case flat::Decl::Kind::kNewType:
            ZX_PANIC("c-codegen for new-types not implemented");
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kService:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("no C name");
        }
        break;
      }

      case flat::Type::Kind::kUntypedNumeric:
        ZX_PANIC("should not have untyped numeric here");
      case flat::Type::Kind::kZxExperimentalPointer:
        ZX_PANIC("C bindings should not be using experimental_pointer");
    }
  }
}

std::string NameIdentifier(SourceSpan name) { return std::string(name.data()); }

std::string NameLibrary(const std::vector<std::unique_ptr<raw::Identifier>>& components) {
  std::string id;
  for (const auto& component : components) {
    if (!id.empty()) {
      id.append(".");
    }
    id.append(component->span().data());
  }
  return id;
}

std::string NameLibrary(const std::vector<std::string_view>& library_name) {
  return utils::StringJoin(library_name, ".");
}

std::string NameLibraryCHeader(const std::vector<std::string_view>& library_name) {
  return utils::StringJoin(library_name, "/") + "/c/fidl.h";
}

std::string NameDiscoverable(const flat::Protocol& protocol) {
  return FormatName(protocol.name, ".", ".");
}

std::string NameMethod(std::string_view protocol_name, const flat::Protocol::Method& method) {
  return std::string(protocol_name) + NameIdentifier(method.name);
}

std::string NameOrdinal(std::string_view method_name) {
  std::string ordinal_name(method_name);
  ordinal_name += "Ordinal";
  return ordinal_name;
}

std::string NameMessage(std::string_view method_name, types::MessageKind kind) {
  std::string message_name(method_name);
  switch (kind) {
    case types::MessageKind::kRequest:
      message_name += "RequestMessage";
      break;
    case types::MessageKind::kResponse:
      message_name += "ResponseMessage";
      break;
    case types::MessageKind::kEvent:
      message_name += "EventMessage";
      break;
  }
  return message_name;
}

std::string NameTable(std::string_view table_name) { return std::string(table_name) + "Table"; }

std::string NamePointer(std::string_view name) {
  std::string pointer_name("Pointer");
  pointer_name += LengthPrefixedString(name);
  return pointer_name;
}

std::string NameMembers(std::string_view name) {
  std::string members_name("Members");
  members_name += LengthPrefixedString(name);
  return members_name;
}

std::string NameFields(std::string_view name) {
  std::string fields_name("Fields");
  fields_name += LengthPrefixedString(name);
  return fields_name;
}

std::string NameFieldsAltField(std::string_view name, uint32_t field_num) {
  std::ostringstream fields_alt_field_name;
  fields_alt_field_name << NameFields(name);
  fields_alt_field_name << "_field";
  fields_alt_field_name << field_num;
  fields_alt_field_name << "_alt_field";
  return fields_alt_field_name.str();
}

std::string NameCodedName(const flat::Name& name) { return FormatName(name, "_", "_"); }

std::string NameCodedNullableName(const flat::Name& name) {
  std::ostringstream nullable_name;
  nullable_name << NameCodedName(name);
  nullable_name << "NullableRef";
  return nullable_name.str();
}

std::string NameCodedHandle(types::HandleSubtype subtype, types::RightsWrappedType rights,
                            types::Nullability nullability) {
  std::string name("Handle");
  name += NameHandleSubtype(subtype);
  name += NameHandleRights(rights);
  name += NameNullability(nullability);
  return name;
}

std::string NameCodedProtocolHandle(std::string_view protocol_name,
                                    types::Nullability nullability) {
  std::string name("Protocol");
  name += LengthPrefixedString(protocol_name);
  name += NameNullability(nullability);
  return name;
}

std::string NameCodedRequestHandle(std::string_view protocol_name, types::Nullability nullability) {
  std::string name("Request");
  name += LengthPrefixedString(protocol_name);
  name += NameNullability(nullability);
  return name;
}

std::string NameCodedArray(std::string_view element_name, uint64_t size) {
  std::string name("Array");
  name += NameSize(size);
  name += "_";
  name += LengthPrefixedString(element_name);
  return name;
}

std::string NameCodedVector(std::string_view element_name, uint64_t max_size,
                            types::Nullability nullability) {
  std::string name("Vector");
  name += NameSize(max_size);
  name += NameNullability(nullability);
  name += LengthPrefixedString(element_name);
  return name;
}

std::string NameCodedString(uint64_t max_size, types::Nullability nullability) {
  std::string name("String");
  name += NameSize(max_size);
  name += NameNullability(nullability);
  return name;
}

std::string NameCodedZxExperimentalPointer(std::string_view pointee_name) {
  std::string name("ZxExperimentalPointer");
  name += LengthPrefixedString(pointee_name);
  return name;
}

}  // namespace fidl
