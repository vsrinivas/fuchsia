// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_PROTO_VALUE_H_
#define SRC_LIB_FIDL_CODEC_PROTO_VALUE_H_

#include "src/lib/fidl_codec/proto/value.pb.h"
#include "src/lib/fidl_codec/visitor.h"

namespace fidl_codec {

// Visitor which convert a fidl_codec value into a protobuf value.
class ProtoVisitor : public fidl_codec::Visitor {
 public:
  ProtoVisitor(proto::Value* dst) : dst_(dst) {}

 private:
  void VisitNullValue(const fidl_codec::NullValue* node, const fidl_codec::Type* for_type) override;
  void VisitRawValue(const fidl_codec::RawValue* node, const fidl_codec::Type* for_type) override;
  void VisitBoolValue(const fidl_codec::BoolValue* node, const fidl_codec::Type* for_type) override;
  void VisitIntegerValue(const fidl_codec::IntegerValue* node,
                         const fidl_codec::Type* for_type) override;
  void VisitActualAndRequestedValue(const fidl_codec::ActualAndRequestedValue* node,
                                    const fidl_codec::Type* for_type) override;
  void VisitDoubleValue(const fidl_codec::DoubleValue* node,
                        const fidl_codec::Type* for_type) override;
  void VisitStringValue(const fidl_codec::StringValue* node,
                        const fidl_codec::Type* for_type) override;
  void VisitHandleValue(const fidl_codec::HandleValue* node,
                        const fidl_codec::Type* for_type) override;
  void VisitUnionValue(const fidl_codec::UnionValue* node,
                       const fidl_codec::Type* for_type) override;
  void VisitStructValue(const fidl_codec::StructValue* node,
                        const fidl_codec::Type* for_type) override;
  void VisitVectorValue(const fidl_codec::VectorValue* node,
                        const fidl_codec::Type* for_type) override;
  void VisitTableValue(const fidl_codec::TableValue* node,
                       const fidl_codec::Type* for_type) override;
  void VisitFidlMessageValue(const fidl_codec::FidlMessageValue* node,
                             const fidl_codec::Type* for_type) override;

 private:
  proto::Value* dst_;
};

std::unique_ptr<Value> DecodeValue(LibraryLoader* loader, const proto::Value& proto_value,
                                   const Type* type);

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_PROTO_VALUE_H_
