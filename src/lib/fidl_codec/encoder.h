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

  static Result EncodeMessage(uint32_t tx_id, uint64_t ordinal, uint8_t flags[3], uint8_t magic,
                              const StructValue& object);

 private:
  Encoder(bool union_as_xunion) : union_as_xunion_(union_as_xunion) {}

  // Pad the current buffer so the next value will be aligned to an 8-byte boundary.
  void Align8();

  // Write an immediate value to the buffer, naturally aligned.
  template <typename T>
  void Write(T t);

  // Write data into our buffer.
  void WriteData(const uint8_t* data, size_t size);
  void WriteData(const std::optional<std::vector<uint8_t>>& data, size_t size);

  // Write a literal value into our buffer.
  template <typename T>
  void WriteValue(std::optional<T>);

  // Execute all functions in the deferred queue. Manipulates the queue such that additional
  // deferred actions caused by each call are executed depth-first. Also pads to 8-byte alignment
  // before each call.
  void Pump();

  // Visit a union which is known to be non-null and which we want encoded immediately at the
  // current position.
  void VisitUnionBody(const UnionValue* node);

  // Visit an object which is known to be non-null and which we want encoded immediately at the
  // current position. If existing_size is specified, it indicates some number of bytes which have
  // already been written that should be considered part of the object for the purpose of
  // calculating member offsets.
  void VisitStructValueBody(const StructValue* node, size_t existing_size = 0);

  // Visit any union and encode it as an XUnion.
  void VisitUnionAsXUnion(const UnionValue* node);

  // Enqueue some work in the deferred queue. Used to defer the encoding of out-of-line data until
  // the in-line data has been fully encoded. Before each callback is called the data will be padded
  // to an 8-byte alignment.
  void Defer(fit::function<void()> cb) { deferred_.push_back(std::move(cb)); }

  // Visitor overrides.
  void VisitRawValue(const RawValue* node) override;
  void VisitStringValue(const StringValue* node) override;
  void VisitBoolValue(const BoolValue* node) override;
  void VisitEnvelopeValue(const EnvelopeValue* node) override;
  void VisitTableValue(const TableValue* node) override;
  void VisitUnionValue(const UnionValue* node) override;
  void VisitXUnionValue(const XUnionValue* node) override;
  void VisitArrayValue(const ArrayValue* node) override;
  void VisitVectorValue(const VectorValue* node) override;
  void VisitEnumValue(const EnumValue* node) override;
  void VisitBitsValue(const BitsValue* node) override;
  void VisitHandleValue(const HandleValue* node) override;
  void VisitStructValue(const StructValue* node) override;
  void VisitU8Value(const NumericValue<uint8_t>* node) override;
  void VisitU16Value(const NumericValue<uint16_t>* node) override;
  void VisitU32Value(const NumericValue<uint32_t>* node) override;
  void VisitU64Value(const NumericValue<uint64_t>* node) override;
  void VisitI8Value(const NumericValue<int8_t>* node) override;
  void VisitI16Value(const NumericValue<int16_t>* node) override;
  void VisitI32Value(const NumericValue<int32_t>* node) override;
  void VisitI64Value(const NumericValue<int64_t>* node) override;
  void VisitF32Value(const NumericValue<float>* node) override;
  void VisitF64Value(const NumericValue<double>* node) override;

  const bool union_as_xunion_;
  std::vector<fit::function<void()>> deferred_;
  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_info_t> handles_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_ENCODER_H_
