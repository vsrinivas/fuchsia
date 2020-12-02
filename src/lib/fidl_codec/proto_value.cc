// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/proto_value.h"

#include "src/lib/fidl_codec/logger.h"
#include "src/lib/fidl_codec/proto/value.pb.h"
#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

void ProtoEncodeStruct(proto::Struct* dst, const fidl_codec::StructValue* node) {
  for (const auto& field : node->fields()) {
    auto proto_field = dst->add_fields();
    proto_field->set_name(field.first->name());
    proto_field->set_id(field.first->id());

    auto value = proto_field->mutable_value();
    ProtoVisitor visitor(value);
    field.second->Visit(&visitor, nullptr);
  }
}

void ProtoVisitor::VisitNullValue(const fidl_codec::NullValue* node,
                                  const fidl_codec::Type* for_type) {
  dst_->set_null_value(true);
}

void ProtoVisitor::VisitRawValue(const fidl_codec::RawValue* node,
                                 const fidl_codec::Type* for_type) {
  dst_->set_raw_value(node->data().data(), node->data().size());
}

void ProtoVisitor::VisitBoolValue(const fidl_codec::BoolValue* node,
                                  const fidl_codec::Type* for_type) {
  dst_->set_bool_value(node->value());
}

void ProtoVisitor::VisitIntegerValue(const fidl_codec::IntegerValue* node,
                                     const fidl_codec::Type* for_type) {
  proto::Integer* integer = dst_->mutable_integer_value();
  integer->set_absolute_value(node->absolute_value());
  integer->set_negative(node->negative());
}

void ProtoVisitor::VisitDoubleValue(const fidl_codec::DoubleValue* node,
                                    const fidl_codec::Type* for_type) {
  dst_->set_double_value(node->value());
}

void ProtoVisitor::VisitStringValue(const fidl_codec::StringValue* node,
                                    const fidl_codec::Type* for_type) {
  dst_->set_string_value(node->string());
}

void ProtoVisitor::VisitHandleValue(const fidl_codec::HandleValue* node,
                                    const fidl_codec::Type* for_type) {
  proto::HandleInfo* handle_info = dst_->mutable_handle_value();
  handle_info->set_handle(node->handle().handle);
  handle_info->set_type(node->handle().type);
  handle_info->set_rights(node->handle().rights);
  handle_info->set_operation(static_cast<int32_t>(node->handle().operation));
}

void ProtoVisitor::VisitUnionValue(const fidl_codec::UnionValue* node,
                                   const fidl_codec::Type* for_type) {
  proto::Union* union_value = dst_->mutable_union_value();
  union_value->set_member(node->member().name());
  ProtoVisitor visitor(union_value->mutable_value());
  node->value()->Visit(&visitor, nullptr);
}

void ProtoVisitor::VisitStructValue(const fidl_codec::StructValue* node,
                                    const fidl_codec::Type* for_type) {
  ProtoEncodeStruct(dst_->mutable_struct_value(), node);
}

void ProtoVisitor::VisitVectorValue(const fidl_codec::VectorValue* node,
                                    const fidl_codec::Type* for_type) {
  proto::Vector* vector_value = dst_->mutable_vector_value();
  for (const auto& value : node->values()) {
    ProtoVisitor visitor(vector_value->add_value());
    value->Visit(&visitor, nullptr);
  }
}

void ProtoVisitor::VisitTableValue(const fidl_codec::TableValue* node,
                                   const fidl_codec::Type* for_type) {
  proto::Table* table_value = dst_->mutable_table_value();
  for (const auto& member : node->members()) {
    proto::Value value;
    ProtoVisitor visitor(&value);
    member.second->Visit(&visitor, nullptr);
    table_value->mutable_members()->insert(google::protobuf::MapPair(member.first->name(), value));
  }
}

void ProtoVisitor::VisitFidlMessageValue(const fidl_codec::FidlMessageValue* node,
                                         const fidl_codec::Type* for_type) {
  proto::FidlMessage* fidl_message = dst_->mutable_fidl_message_value();
  fidl_message->set_txid(node->txid());
  fidl_message->set_ordinal(node->ordinal());
  fidl_message->set_global_errors(node->global_errors());
  fidl_message->set_epitaph_error(node->epitaph_error());
  fidl_message->set_received(node->received());
  fidl_message->set_is_request(node->is_request());
  fidl_message->set_unknown_direction(node->unknown_direction());
  if (node->method() != nullptr) {
    fidl_message->set_interface(node->method()->enclosing_interface().name());
    fidl_message->set_method(node->method()->name());
  }
  fidl_message->set_raw_bytes(node->bytes().data(), node->bytes().size());
  for (const auto& handle : node->handles()) {
    proto::HandleInfo* handle_info = fidl_message->add_handle();
    handle_info->set_handle(handle.handle);
    handle_info->set_type(handle.type);
    handle_info->set_rights(handle.rights);
    handle_info->set_operation(-1);
  }
  if (node->decoded_request() != nullptr) {
    fidl_message->set_has_request(true);
    ProtoEncodeStruct(fidl_message->mutable_decoded_request(), node->decoded_request());
  }
  fidl_message->set_request_errors(node->request_errors());
  if (node->decoded_response() != nullptr) {
    fidl_message->set_has_response(true);
    ProtoEncodeStruct(fidl_message->mutable_decoded_response(), node->decoded_response());
  }
  fidl_message->set_response_errors(node->response_errors());
}

std::unique_ptr<StructValue> DecodeStruct(LibraryLoader* loader, const proto::Struct& proto_struct,
                                          const Struct& struct_definition) {
  bool ok = true;
  auto struct_value = std::make_unique<fidl_codec::StructValue>(struct_definition);
  for (const auto& proto_field : proto_struct.fields()) {
    const fidl_codec::StructMember* member =
        struct_definition.SearchMember(proto_field.name(), proto_field.id());
    if (member == nullptr) {
      FX_LOGS_OR_CAPTURE(ERROR) << "Member " << proto_field.name() << ":" << proto_field.id()
                                << " not found in " << struct_definition.name() << '.';
      ok = false;
    } else {
      std::unique_ptr<fidl_codec::Value> value =
          DecodeValue(loader, proto_field.value(), member->type());
      if (value == nullptr) {
        ok = false;
      } else {
        struct_value->AddField(member, std::move(value));
      }
    }
  }
  if (!ok) {
    return nullptr;
  }
  return struct_value;
}

std::unique_ptr<Value> DecodeValue(LibraryLoader* loader, const proto::Value& proto_value,
                                   const Type* type) {
  switch (proto_value.Kind_case()) {
    case proto::Value::kNullValue:
      return std::make_unique<fidl_codec::NullValue>();
    case proto::Value::kRawValue:
      return std::make_unique<fidl_codec::RawValue>(
          reinterpret_cast<const uint8_t*>(proto_value.raw_value().data()),
          proto_value.raw_value().size());
    case proto::Value::kBoolValue:
      return std::make_unique<fidl_codec::BoolValue>(proto_value.bool_value());
    case proto::Value::kIntegerValue:
      return std::make_unique<fidl_codec::IntegerValue>(
          proto_value.integer_value().absolute_value(), proto_value.integer_value().negative());
    case proto::Value::kDoubleValue:
      return std::make_unique<fidl_codec::DoubleValue>(proto_value.double_value());
    case proto::Value::kStringValue:
      return std::make_unique<fidl_codec::StringValue>(proto_value.string_value());
    case proto::Value::kHandleValue: {
      const proto::HandleInfo& proto_handle_info = proto_value.handle_value();
      zx_handle_disposition_t handle_disposition;
      handle_disposition.operation = static_cast<zx_handle_op_t>(proto_handle_info.operation());
      handle_disposition.handle = proto_handle_info.handle();
      handle_disposition.type = proto_handle_info.type();
      handle_disposition.rights = proto_handle_info.rights();
      handle_disposition.result = ZX_OK;
      return std::make_unique<fidl_codec::HandleValue>(handle_disposition);
    }
    case proto::Value::kUnionValue: {
      auto union_type = type->AsUnionType();
      if (union_type == nullptr) {
        FX_LOGS_OR_CAPTURE(ERROR) << "Type of union value should be union.";
        return nullptr;
      }
      fidl_codec::UnionMember* member =
          union_type->union_definition().SearchMember(proto_value.union_value().member());
      if (member == nullptr) {
        FX_LOGS_OR_CAPTURE(ERROR) << "Member " << proto_value.union_value().member()
                                  << " not found in union " << union_type->union_definition().name()
                                  << '.';
        return nullptr;
      }
      std::unique_ptr<fidl_codec::Value> union_value =
          DecodeValue(loader, proto_value.union_value().value(), member->type());
      if (union_value == nullptr) {
        return nullptr;
      }
      return std::make_unique<fidl_codec::UnionValue>(*member, std::move(union_value));
    }
    case proto::Value::kStructValue: {
      auto struct_type = type->AsStructType();
      if (struct_type == nullptr) {
        FX_LOGS_OR_CAPTURE(ERROR) << "Type of struct value should be struct.";
        return nullptr;
      }
      return DecodeStruct(loader, proto_value.struct_value(), struct_type->struct_definition());
    }
    case proto::Value::kVectorValue: {
      const fidl_codec::Type* component_type = type->GetComponentType();
      if (component_type == nullptr) {
        FX_LOGS_OR_CAPTURE(ERROR) << "Type of vector should be array or vector.";
        return nullptr;
      }
      bool ok = true;
      auto vector_value = std::make_unique<fidl_codec::VectorValue>();
      const proto::Vector& proto_vector_value = proto_value.vector_value();
      for (int index = 0; index < proto_vector_value.value_size(); ++index) {
        std::unique_ptr<fidl_codec::Value> value =
            DecodeValue(loader, proto_vector_value.value(index), component_type);
        if (value == nullptr) {
          ok = false;
        } else {
          vector_value->AddValue(std::move(value));
        }
      }
      if (!ok) {
        return nullptr;
      }
      return vector_value;
    }
    case proto::Value::kTableValue: {
      auto table_type = type->AsTableType();
      if (table_type == nullptr) {
        FX_LOGS_OR_CAPTURE(ERROR) << "Type of table value should be table.";
        return nullptr;
      }
      bool ok = true;
      auto table_value = std::make_unique<fidl_codec::TableValue>(table_type->table_definition());
      for (const auto& proto_member : proto_value.table_value().members()) {
        const fidl_codec::TableMember* member =
            table_type->table_definition().GetMember(proto_member.first);
        if (member == nullptr) {
          FX_LOGS_OR_CAPTURE(ERROR) << "Member " << proto_member.first << " not found in "
                                    << table_type->table_definition().name() << '.';
          ok = false;
        } else {
          std::unique_ptr<fidl_codec::Value> value =
              DecodeValue(loader, proto_member.second, member->type());
          if (value == nullptr) {
            ok = false;
          } else {
            table_value->AddMember(member, std::move(value));
          }
        }
      }
      if (!ok) {
        return nullptr;
      }
      return table_value;
    }
    case proto::Value::kFidlMessageValue: {
      const proto::FidlMessage& proto_message = proto_value.fidl_message_value();
      const fidl_codec::InterfaceMethod* method = nullptr;
      // We need to check loader because some tests have a null library loader.
      if (loader != nullptr) {
        const std::vector<const fidl_codec::InterfaceMethod*>* methods =
            loader->GetByOrdinal(proto_message.ordinal());
        if ((methods != nullptr) && !methods->empty()) {
          method = (*methods)[0];
        }
      }
      auto message = std::make_unique<fidl_codec::FidlMessageValue>(
          proto_message.txid(), proto_message.ordinal(), proto_message.global_errors(),
          proto_message.epitaph_error(), proto_message.received(), proto_message.is_request(),
          proto_message.unknown_direction(), method,
          reinterpret_cast<const uint8_t*>(proto_message.raw_bytes().data()),
          proto_message.raw_bytes().size(), proto_message.request_errors(),
          proto_message.response_errors());
      for (int index = 0; index < proto_message.handle_size(); ++index) {
        const proto::HandleInfo proto_handle_info = proto_message.handle(index);
        zx_handle_disposition_t handle_disposition;
        handle_disposition.operation = fidl_codec::kNoHandleDisposition;
        handle_disposition.handle = proto_handle_info.handle();
        handle_disposition.type = proto_handle_info.type();
        handle_disposition.rights = proto_handle_info.rights();
        handle_disposition.result = ZX_OK;
        message->add_handle(handle_disposition);
      }
      if (method != nullptr) {
        // We can have a null method if we replay a file with a different state (for example, we
        // don't have all the json we had when the event has been saved).
        bool ok = true;
        if (proto_message.has_request()) {
          if (method->request() == nullptr) {
            FX_LOGS_OR_CAPTURE(ERROR)
                << "Request without request defined in " << method->name() << '.';
            ok = false;
          } else {
            message->set_decoded_request(
                DecodeStruct(loader, proto_message.decoded_request(), *method->request()));
          }
        }
        if (proto_message.has_response()) {
          if (method->response() == nullptr) {
            FX_LOGS_OR_CAPTURE(ERROR)
                << "Response without response defined in " << method->name() << '.';
            ok = false;
          } else {
            message->set_decoded_response(
                DecodeStruct(loader, proto_message.decoded_response(), *method->response()));
          }
        }
        if (!ok) {
          return nullptr;
        }
      }
      return message;
    }
    default:
      FX_LOGS_OR_CAPTURE(ERROR) << "Unknown value.";
      return nullptr;
  }
}

}  // namespace fidl_codec
