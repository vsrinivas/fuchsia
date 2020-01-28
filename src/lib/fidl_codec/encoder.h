// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_ENCODER_H_
#define SRC_LIB_FIDL_CODEC_ENCODER_H_

#include <optional>
#include <vector>

#include "lib/fit/function.h"
#include "src/lib/fidl_codec/visitor.h"

namespace fidl_codec {

class Encoder : public Visitor {
 public:
  struct Result {
    std::vector<uint8_t> bytes;
    std::vector<zx_handle_info_t> handles;
  };

  static Result EncodeMessage(uint32_t tx_id, uint64_t ordinal, const uint8_t flags[3],
                              uint8_t magic, const StructValue& object);

  // Write a literal value into our buffer.
  template <typename T>
  void WriteValue(T value) {
    WriteData(reinterpret_cast<uint8_t*>(&value), sizeof(T));
  }

 private:
  Encoder() = default;

  // Reserve space in the buffer for one object.
  size_t AllocateObject(size_t object_size);

  // Write data into our buffer.
  void WriteData(const uint8_t* data, size_t size);
  void WriteData(const std::vector<uint8_t>& data) { WriteData(data.data(), data.size()); }

  // Encode a value in an envelope.
  void EncodeEnvelope(const Value* value, const Type* for_type);

  // Visit an object which is known to be non-null and which we want encoded immediately at the
  // current position. If existing_size is specified, it indicates some number of bytes which have
  // already been written that should be considered part of the object for the purpose of
  // calculating member offsets.
  void VisitStructValueBody(size_t offset, const StructValue* node);

  // Visitor overrides.
  void VisitInvalidValue(const InvalidValue* node, const Type* for_type) override;
  void VisitNullValue(const NullValue* node, const Type* for_type) override;
  void VisitRawValue(const RawValue* node, const Type* for_type) override;
  void VisitBoolValue(const BoolValue* node, const Type* for_type) override;
  void VisitIntegerValue(const IntegerValue* node, const Type* for_type) override;
  void VisitDoubleValue(const DoubleValue* node, const Type* for_type) override;
  void VisitStringValue(const StringValue* node, const Type* for_type) override;
  void VisitHandleValue(const HandleValue* node, const Type* for_type) override;
  void VisitUnionValue(const UnionValue* node, const Type* for_type) override;
  void VisitStructValue(const StructValue* node, const Type* for_type) override;
  void VisitVectorValue(const VectorValue* node, const Type* for_type) override;
  void VisitTableValue(const TableValue* node, const Type* for_type) override;

  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_info_t> handles_;
  // Offset we are currently using to write into the buffer.
  size_t current_offset_ = 0;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_ENCODER_H_
