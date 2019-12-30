// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/encoder.h"

#include <algorithm>

#include "lib/fidl/txn_header.h"
#include "src/lib/fidl_codec/type_visitor.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

// Currently only the String type can generate a null value.
class NullVisitor : public TypeVisitor {
 public:
  explicit NullVisitor(Encoder* encoder) : encoder_(encoder) {}

 private:
  void VisitType(const Type* type) override {
    FXL_LOG(FATAL) << "Type " << type->Name() << " can't be null.";
  }

  void VisitStringType(const StringType* type) override {
    FXL_DCHECK(type->Nullable());
    encoder_->WriteValue<uint64_t>(0);
    encoder_->WriteValue<uint64_t>(0);
  }

  Encoder* encoder_;
};

Encoder::Result Encoder::EncodeMessage(uint32_t tx_id, uint64_t ordinal, uint8_t flags[3],
                                       uint8_t magic, const StructValue& object) {
  Encoder encoder(flags[0] & FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG);

  size_t object_size = object.struct_definition().Size(encoder.unions_are_xunions_);
  encoder.AllocateObject(object_size);
  encoder.WriteValue(tx_id);
  encoder.WriteValue(flags[0]);
  encoder.WriteValue(flags[1]);
  encoder.WriteValue(flags[2]);
  encoder.WriteValue(magic);
  encoder.WriteValue(ordinal);
  FXL_DCHECK(sizeof(fidl_message_header_t) == encoder.current_offset_);

  // The primary object offsets include the header size, so the offset of the object is zero.
  encoder.VisitStructValueBody(0, &object);

  return Result{std::move(encoder.bytes_), std::move(encoder.handles_)};
}

size_t Encoder::AllocateObject(size_t size) {
  size_t object_offset = bytes_.size();
  bytes_.resize((bytes_.size() + size + 7) & ~7);
  return object_offset;
}

void Encoder::WriteData(const uint8_t* data, size_t size) {
  FXL_DCHECK(current_offset_ + size <= bytes_.size())
      << "needs " << size << " bytes at offset " << current_offset_ << " buffer size is "
      << bytes_.size();
  std::copy(data, data + size, bytes_.data() + current_offset_);
  current_offset_ += size;
}

void Encoder::VisitUnionBody(const UnionValue* node) {
  FXL_DCHECK(node->member() != nullptr);
  WriteValue<uint32_t>(node->member()->ordinal());
  node->value()->Visit(this);
}

void Encoder::VisitStructValueBody(size_t offset, const StructValue* node) {
  FXL_DCHECK(!node->IsNull());

  for (const auto& member : node->struct_definition().members()) {
    auto it = node->fields().find(member.get());
    FXL_DCHECK(it != node->fields().end());
    current_offset_ = offset + (unions_are_xunions_ ? member->v1_offset() : member->v0_offset());
    it->second->Visit(this);
  }
}

void Encoder::VisitUnionAsXUnion(const UnionValue* node) {
  uint32_t ordinal = (node->member() == nullptr) ? 0 : node->member()->ordinal();

  FXL_DCHECK(ordinal || node->IsNull() || node->value()->IsNull())
      << "Invalid xunion field '" << node->member()->name() << "'";

  auto field = node->value().get();
  WriteValue<uint64_t>(ordinal);

  if (field) {
    field->Visit(this);
  } else {
    // Empty envelope
    WriteValue<uint32_t>(0);
    WriteValue<uint32_t>(0);
    WriteValue<uint64_t>(0);
  }
}

void Encoder::VisitInvalidValue(const InvalidValue* node) {
  FXL_LOG(FATAL) << "Can't encode invalid data.";
}

void Encoder::VisitNullValue(const NullValue* node) {
  NullVisitor null_visitor(this);
  node->type()->Visit(&null_visitor);
}

void Encoder::VisitRawValue(const RawValue* node) {
  const auto& data = node->data();
  if (data) {
    WriteData(data->data(), data->size());
  }
}

void Encoder::VisitStringValue(const StringValue* node) {
  WriteValue<uint64_t>(node->string().size());
  WriteValue<uint64_t>(UINTPTR_MAX);
  current_offset_ = AllocateObject(node->string().size());
  WriteData(reinterpret_cast<const uint8_t*>(node->string().data()), node->string().size());
}

void Encoder::VisitBoolValue(const BoolValue* node) {
  if (auto value = node->value()) {
    WriteValue<uint8_t>(*value);
  } else {
    WriteValue<uint8_t>(false);
  }
}

void Encoder::VisitEnvelopeValue(const EnvelopeValue* node) {
  WriteValue<uint32_t>(node->num_bytes());
  WriteValue<uint32_t>(node->num_handles());

  if (node->IsNull() || (node->value() == nullptr)) {
    WriteValue<uint64_t>(0);
  } else {
    WriteValue<uint64_t>(UINTPTR_MAX);
    current_offset_ = AllocateObject(node->type()->InlineSize(unions_are_xunions_));
    node->value()->Visit(this);
  }
}

void Encoder::VisitTableValue(const TableValue* node) {
  WriteValue<uint64_t>(node->highest_member());
  WriteValue<uint64_t>(UINTPTR_MAX);

  constexpr size_t kEnvelopeSize = 2 * sizeof(uint32_t) + sizeof(uint64_t);
  size_t offset = AllocateObject(node->highest_member() * kEnvelopeSize);

  for (Ordinal32 i = 1; i <= node->highest_member(); ++i) {
    current_offset_ = offset;
    auto it = node->members().find(node->table_definition().members()[i].get());
    if ((it == node->members().end()) || it->second->IsNull()) {
      WriteValue<uint32_t>(0);
      WriteValue<uint32_t>(0);
      WriteValue<uint64_t>(0);
    } else {
      it->second->Visit(this);
    }
    offset += kEnvelopeSize;
  }
}

void Encoder::VisitUnionValue(const UnionValue* node) {
  if (unions_are_xunions_) {
    VisitUnionAsXUnion(node);
  } else if (!node->type()->Nullable()) {
    VisitUnionBody(node);
  } else if (node->IsNull()) {
    WriteValue<uint64_t>(0);
  } else {
    WriteValue<uint64_t>(UINTPTR_MAX);
    current_offset_ = AllocateObject(node->union_definition().size());
    VisitUnionBody(node);
  }
}

void Encoder::VisitXUnionValue(const XUnionValue* node) { VisitUnionAsXUnion(node); }

void Encoder::VisitArrayValue(const ArrayValue* node) {
  size_t component_size = node->type()->GetComponentType()->InlineSize(unions_are_xunions_);
  size_t offset = current_offset_;
  for (const auto& value : node->values()) {
    current_offset_ = offset;
    value->Visit(this);
    offset += component_size;
  }
}

void Encoder::VisitVectorValue(const VectorValue* node) {
  if (node->IsNull()) {
    WriteValue<uint64_t>(0);
    WriteValue<uint64_t>(0);
  } else {
    WriteValue<uint64_t>(node->size());
    WriteValue<uint64_t>(UINTPTR_MAX);
    size_t component_size = node->type()->GetComponentType()->InlineSize(unions_are_xunions_);
    size_t offset = AllocateObject(component_size * node->values().size());
    for (const auto& value : node->values()) {
      current_offset_ = offset;
      value->Visit(this);
      offset += component_size;
    }
  }
}

void Encoder::VisitEnumValue(const EnumValue* node) {
  if (node->data()) {
    WriteData(node->data()->data(), node->enum_definition().size());
  }
}

void Encoder::VisitBitsValue(const BitsValue* node) {
  if (node->data()) {
    WriteData(node->data()->data(), node->bits_definition().size());
  }
}

void Encoder::VisitHandleValue(const HandleValue* node) {
  if (node->handle().handle == FIDL_HANDLE_ABSENT) {
    WriteValue<uint32_t>(FIDL_HANDLE_ABSENT);
  } else {
    WriteValue<uint32_t>(FIDL_HANDLE_PRESENT);
    handles_.push_back(node->handle());
  }
}

void Encoder::VisitStructValue(const StructValue* node) {
  if (!node->type()->Nullable()) {
    VisitStructValueBody(current_offset_, node);
  } else if (node->IsNull()) {
    WriteValue<uint64_t>(0);
  } else {
    WriteValue<uint64_t>(UINTPTR_MAX);
    size_t object_size = node->struct_definition().Size(unions_are_xunions_);
    VisitStructValueBody(AllocateObject(object_size), node);
  }
}

void Encoder::VisitU8Value(const NumericValue<uint8_t>* node) { WriteValue(node->value()); }

void Encoder::VisitU16Value(const NumericValue<uint16_t>* node) { WriteValue(node->value()); }

void Encoder::VisitU32Value(const NumericValue<uint32_t>* node) { WriteValue(node->value()); }

void Encoder::VisitU64Value(const NumericValue<uint64_t>* node) { WriteValue(node->value()); }

void Encoder::VisitI8Value(const NumericValue<int8_t>* node) { WriteValue(node->value()); }

void Encoder::VisitI16Value(const NumericValue<int16_t>* node) { WriteValue(node->value()); }

void Encoder::VisitI32Value(const NumericValue<int32_t>* node) { WriteValue(node->value()); }

void Encoder::VisitI64Value(const NumericValue<int64_t>* node) { WriteValue(node->value()); }

void Encoder::VisitF32Value(const NumericValue<float>* node) { WriteValue(node->value()); }

void Encoder::VisitF64Value(const NumericValue<double>* node) { WriteValue(node->value()); }

}  // namespace fidl_codec
