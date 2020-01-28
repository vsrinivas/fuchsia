// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_JOSH_LIB_OBJECT_CONVERTER_H_
#define SRC_DEVELOPER_SHELL_JOSH_LIB_OBJECT_CONVERTER_H_

#include "src/lib/fidl_codec/type_visitor.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::fidl {

// TypeVisitor which converts a quickjs JSValue into a fidl_codec Value.
class ObjectConverter : public fidl_codec::TypeVisitor {
 public:
  static std::unique_ptr<fidl_codec::Value> Convert(JSContext* ctx, fidl_codec::Struct* st,
                                                    const JSValue& value) {
    ObjectConverter converter(ctx, value);
    st->VisitAsType(&converter);
    return std::move(converter.result_);
  }

  static std::unique_ptr<fidl_codec::Value> Convert(JSContext* ctx, const fidl_codec::Type* type,
                                                    const JSValue& value) {
    ObjectConverter converter(ctx, value);
    type->Visit(&converter);
    return std::move(converter.result_);
  }

 private:
  explicit ObjectConverter(JSContext* ctx, const JSValue& value) : ctx_(ctx), value_(value) {}

  bool HandleNull(const fidl_codec::Type* type);

  template <typename T>
  void VisitAnyList(const T* type, std::optional<size_t> count);

  void VisitAnyInteger(bool is_signed);
  void VisitAnyFloat();

  // TypeVisitor implementation
  void VisitType(const fidl_codec::Type* type) override;
  void VisitStringType(const fidl_codec::StringType* type) override;
  void VisitBoolType(const fidl_codec::BoolType* type) override;
  void VisitStructType(const fidl_codec::StructType* type) override;
  void VisitTableType(const fidl_codec::TableType* type) override;
  void VisitUnionType(const fidl_codec::UnionType* type) override;
  void VisitArrayType(const fidl_codec::ArrayType* type) override;
  void VisitVectorType(const fidl_codec::VectorType* type) override;
  void VisitEnumType(const fidl_codec::EnumType* type) override;
  void VisitBitsType(const fidl_codec::BitsType* type) override;
  void VisitHandleType(const fidl_codec::HandleType* type) override;
  void VisitUint8Type(const fidl_codec::Uint8Type* type) override;
  void VisitUint16Type(const fidl_codec::Uint16Type* type) override;
  void VisitUint32Type(const fidl_codec::Uint32Type* type) override;
  void VisitUint64Type(const fidl_codec::Uint64Type* type) override;
  void VisitInt8Type(const fidl_codec::Int8Type* type) override;
  void VisitInt16Type(const fidl_codec::Int16Type* type) override;
  void VisitInt32Type(const fidl_codec::Int32Type* type) override;
  void VisitInt64Type(const fidl_codec::Int64Type* type) override;
  void VisitFloat32Type(const fidl_codec::Float32Type* type) override;
  void VisitFloat64Type(const fidl_codec::Float64Type* type) override;

  JSContext* ctx_;
  const JSValue& value_;
  std::unique_ptr<fidl_codec::Value> result_;
};

}  // namespace shell::fidl

#endif  // SRC_DEVELOPER_SHELL_JOSH_LIB_OBJECT_CONVERTER_H_
