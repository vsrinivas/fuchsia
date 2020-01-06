// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/encoder.h"

#include <lib/fidl/txn_header.h>

#include <algorithm>

#include <src/lib/fxl/logging.h>

#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

Encoder::Result Encoder::EncodeMessage(uint32_t tx_id, uint64_t ordinal, uint8_t flags[3],
                                       uint8_t magic, const StructValue& object) {
  Encoder encoder(flags[0] & FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG);

  encoder.Write(tx_id);
  encoder.Write(flags[0]);
  encoder.Write(flags[1]);
  encoder.Write(flags[2]);
  encoder.Write(magic);
  encoder.Write(ordinal);
  FXL_DCHECK(sizeof(fidl_message_header_t) == encoder.bytes_.size());

  // The offsets for the primary object include the header size, so we have to specify an existing
  // size equal to the size of what we've already written above.
  encoder.VisitStructValueBody(&object, /*existing_size=*/sizeof(fidl_message_header_t));

  encoder.Pump();
  encoder.Align8();

  return Result{std::move(encoder.bytes_), std::move(encoder.handles_)};
}

void Encoder::Align8() { bytes_.resize((bytes_.size() + 7) & ~7); }

template <typename T>
void Encoder::Write(T t) {
  auto old_size = bytes_.size();
  bytes_.resize(old_size + sizeof(T));
  *reinterpret_cast<T*>(bytes_.data() + old_size) = t;
}

void Encoder::WriteData(const uint8_t* data, size_t size) {
  if (data == nullptr) {
    bytes_.resize(bytes_.size() + size);
  } else {
    std::copy(data, data + size, std::back_inserter(bytes_));
  }
}

void Encoder::WriteData(const std::optional<std::vector<uint8_t>>& data, size_t size) {
  const uint8_t* data_ptr = nullptr;

  if (data && data->size() >= size) {
    data_ptr = data->data();
  }

  WriteData(data_ptr, size);
}

template <typename T>
void Encoder::WriteValue(std::optional<T> value) {
  T real_value = 0;

  if (value) {
    real_value = *value;
  }

  WriteData(reinterpret_cast<uint8_t*>(&real_value), sizeof(T));
}

void Encoder::Pump() {
  if (deferred_.empty()) {
    return;
  }

  auto deferred = std::move(deferred_);
  deferred_.clear();

  for (const auto& func : deferred) {
    Align8();
    func();
    Pump();
  }
}

void Encoder::VisitUnionBody(const UnionValue* node) {
  FXL_DCHECK(!node->is_null());
  auto target_size = bytes_.size() + node->definition().size();
  const size_t align_to = node->definition().alignment();
  const size_t align_mask = align_to - 1;

  FXL_CHECK(!(align_to & align_mask));
  bytes_.resize((bytes_.size() + align_mask) & ~align_mask);

  uint32_t tag = 0;
  for (const auto& member : node->definition().members()) {
    if (!member->reserved()) {
      if (member->name() == node->field().name()) {
        auto target_offset = bytes_.size() + member->offset();
        Write<uint32_t>(tag);
        bytes_.resize(target_offset);
        if (node->field().value() != nullptr) {
          node->field().value()->Visit(this);
        }
        bytes_.resize(target_size);
        return;
      }

      tag++;
    }
  }

  FXL_NOTREACHED() << "Invalid union field '" << node->field().name() << "'";
}

void Encoder::VisitStructValueBody(const StructValue* node, size_t existing_size) {
  FXL_DCHECK(!node->is_null());
  FXL_DCHECK(existing_size <= bytes_.size());

  size_t object_offset = bytes_.size() - existing_size;
  size_t object_size =
      union_as_xunion_ ? node->struct_definition().v1_size() : node->struct_definition().v0_size();

  for (const auto& member : node->struct_definition().members()) {
    auto it = node->fields().find(std::string(member->name()));
    FXL_DCHECK(it != node->fields().end());
    // Pad the buffer so the next object appended will be at the offset of the member.
    bytes_.resize(object_offset + (union_as_xunion_ ? member->v1_offset() : member->v0_offset()));
    it->second->Visit(this);
  }

  FXL_DCHECK(bytes_.size() <= object_offset + object_size);
  bytes_.resize(object_offset + object_size);
}

void Encoder::VisitUnionAsXUnion(const UnionValue* node) {
  uint32_t ordinal = 0;
  for (const auto& member : node->definition().members()) {
    if (member->name() == node->field().name()) {
      ordinal = member->ordinal();
      break;
    }
  }

  FXL_DCHECK(ordinal || node->is_null() || node->field().value()->is_null())
      << "Invalid xunion field '" << node->field().name() << "'";

  auto field = node->field().value().get();
  Write<uint64_t>(ordinal);

  if (field) {
    field->Visit(this);
  } else {
    // Empty envelope
    Write<uint32_t>(0);
    Write<uint32_t>(0);
  }
}

void Encoder::VisitRawValue(const RawValue* node) {
  const auto& data = node->data();
  size_t size = 0;

  if (data) {
    size = data->size();
  }

  WriteData(data, size);
}

void Encoder::VisitStringValue(const StringValue* node) {
  if (node->is_null()) {
    Write<uint64_t>(0);
    Write<uint64_t>(0);
  } else {
    Write<uint64_t>(node->size());
    Write<uint64_t>(UINTPTR_MAX);
    Defer([this, node]() mutable {
      const uint8_t* data = nullptr;
      if (node->string()) {
        data = reinterpret_cast<const uint8_t*>(node->string()->data());
      }

      WriteData(data, node->size());
    });
  }
}

void Encoder::VisitBoolValue(const BoolValue* node) {
  if (auto value = node->value()) {
    bytes_.push_back(*value);
  } else {
    bytes_.push_back(false);
  }
}

void Encoder::VisitEnvelopeValue(const EnvelopeValue* node) {
  Write<uint32_t>(node->num_bytes());
  Write<uint32_t>(node->num_handles());

  if (node->is_null() || (node->value() == nullptr)) {
    Write<uint64_t>(0);
  } else {
    Write<uint64_t>(UINTPTR_MAX);
    Defer([this, node]() mutable { node->value()->Visit(this); });
  }
}

void Encoder::VisitTableValue(const TableValue* node) {
  const auto& envelopes = node->envelopes();

  Write<uint64_t>(envelopes.size());
  Write<uint64_t>(UINTPTR_MAX);

  Defer([this, &envelopes]() mutable {
    for (const auto& field : envelopes) {
      field.value()->Visit(this);
    }
  });
}

void Encoder::VisitUnionValue(const UnionValue* node) {
  if (union_as_xunion_) {
    VisitUnionAsXUnion(node);
  } else if (!node->type()->Nullable()) {
    VisitUnionBody(node);
  } else if (node->is_null()) {
    Write<uint64_t>(0);
  } else {
    Write<uint64_t>(UINTPTR_MAX);
    Defer([this, node]() mutable { VisitUnionBody(node); });
  }
}

void Encoder::VisitXUnionValue(const XUnionValue* node) { VisitUnionAsXUnion(node); }

void Encoder::VisitArrayValue(const ArrayValue* node) {
  for (const auto& value : node->values()) {
    value->Visit(this);
  }
}

void Encoder::VisitVectorValue(const VectorValue* node) {
  if (node->is_null()) {
    Write<uint64_t>(0);
    Write<uint64_t>(0);
  } else {
    Write<uint64_t>(node->size());
    Write<uint64_t>(UINTPTR_MAX);
    Defer([this, node]() mutable {
      for (const auto& value : node->values()) {
        value->Visit(this);
      }
    });
  }
}

void Encoder::VisitEnumValue(const EnumValue* node) {
  WriteData(node->data(), node->enum_definition().size());
}

void Encoder::VisitBitsValue(const BitsValue* node) {
  WriteData(node->data(), node->bits_definition().size());
}

void Encoder::VisitHandleValue(const HandleValue* node) {
  if (node->handle().handle == FIDL_HANDLE_ABSENT) {
    Write<uint32_t>(FIDL_HANDLE_ABSENT);
  } else {
    Write<uint32_t>(FIDL_HANDLE_PRESENT);
    Defer([this, node]() mutable { handles_.push_back(node->handle()); });
  }
}

void Encoder::VisitStructValue(const StructValue* node) {
  if (!node->type()->Nullable()) {
    VisitStructValueBody(node);
  } else if (node->is_null()) {
    Write<uint64_t>(0);
  } else {
    Write<uint64_t>(UINTPTR_MAX);
    Defer([this, node]() mutable { VisitStructValueBody(node); });
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
