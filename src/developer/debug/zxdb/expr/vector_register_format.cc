// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vector_register_format.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

using debug::RegisterID;

const char kVectorRegisterFormatStr_Signed8[] = "i8";
const char kVectorRegisterFormatStr_Unsigned8[] = "u8";
const char kVectorRegisterFormatStr_Signed16[] = "i16";
const char kVectorRegisterFormatStr_Unsigned16[] = "u16";
const char kVectorRegisterFormatStr_Signed32[] = "i32";
const char kVectorRegisterFormatStr_Unsigned32[] = "u32";
const char kVectorRegisterFormatStr_Signed64[] = "i64";
const char kVectorRegisterFormatStr_Unsigned64[] = "u64";
const char kVectorRegisterFormatStr_Signed128[] = "i128";
const char kVectorRegisterFormatStr_Unsigned128[] = "u128";
const char kVectorRegisterFormatStr_Float[] = "float";
const char kVectorRegisterFormatStr_Double[] = "double";

const char* VectorRegisterFormatToString(VectorRegisterFormat fmt) {
  switch (fmt) {
    case VectorRegisterFormat::kSigned8:
      return kVectorRegisterFormatStr_Signed8;
    case VectorRegisterFormat::kUnsigned8:
      return kVectorRegisterFormatStr_Unsigned8;
    case VectorRegisterFormat::kSigned16:
      return kVectorRegisterFormatStr_Signed16;
    case VectorRegisterFormat::kUnsigned16:
      return kVectorRegisterFormatStr_Unsigned16;
    case VectorRegisterFormat::kSigned32:
      return kVectorRegisterFormatStr_Signed32;
    case VectorRegisterFormat::kUnsigned32:
      return kVectorRegisterFormatStr_Unsigned32;
    case VectorRegisterFormat::kSigned64:
      return kVectorRegisterFormatStr_Signed64;
    case VectorRegisterFormat::kUnsigned64:
      return kVectorRegisterFormatStr_Unsigned64;
    case VectorRegisterFormat::kSigned128:
      return kVectorRegisterFormatStr_Signed128;
    case VectorRegisterFormat::kUnsigned128:
      return kVectorRegisterFormatStr_Unsigned128;
    case VectorRegisterFormat::kFloat:
      return kVectorRegisterFormatStr_Float;
    case VectorRegisterFormat::kDouble:
      return kVectorRegisterFormatStr_Double;
  }
  FX_NOTREACHED();
  return "";
}

std::optional<VectorRegisterFormat> StringToVectorRegisterFormat(const std::string& fmt) {
  if (fmt == kVectorRegisterFormatStr_Signed8)
    return VectorRegisterFormat::kSigned8;
  if (fmt == kVectorRegisterFormatStr_Unsigned8)
    return VectorRegisterFormat::kUnsigned8;

  if (fmt == kVectorRegisterFormatStr_Signed16)
    return VectorRegisterFormat::kSigned16;
  if (fmt == kVectorRegisterFormatStr_Unsigned16)
    return VectorRegisterFormat::kUnsigned16;

  if (fmt == kVectorRegisterFormatStr_Signed32)
    return VectorRegisterFormat::kSigned32;
  if (fmt == kVectorRegisterFormatStr_Unsigned32)
    return VectorRegisterFormat::kUnsigned32;

  if (fmt == kVectorRegisterFormatStr_Signed64)
    return VectorRegisterFormat::kSigned64;
  if (fmt == kVectorRegisterFormatStr_Unsigned64)
    return VectorRegisterFormat::kUnsigned64;

  if (fmt == kVectorRegisterFormatStr_Signed128)
    return VectorRegisterFormat::kSigned128;
  if (fmt == kVectorRegisterFormatStr_Unsigned128)
    return VectorRegisterFormat::kUnsigned128;

  if (fmt == kVectorRegisterFormatStr_Float)
    return VectorRegisterFormat::kFloat;
  if (fmt == kVectorRegisterFormatStr_Double)
    return VectorRegisterFormat::kDouble;

  return std::nullopt;
}

ExprValue VectorRegisterToValue(RegisterID id, VectorRegisterFormat fmt,
                                std::vector<uint8_t> data) {
  size_t byte_size = 1;
  int base_type = BaseType::kBaseTypeNone;
  const char* type_name = "";

  switch (fmt) {
    case VectorRegisterFormat::kSigned8:
      byte_size = 1;
      base_type = BaseType::kBaseTypeSigned;
      type_name = "int8_t";
      break;
    case VectorRegisterFormat::kUnsigned8:
      byte_size = 1;
      base_type = BaseType::kBaseTypeUnsigned;
      type_name = "uint8_t";
      break;
    case VectorRegisterFormat::kSigned16:
      byte_size = 2;
      base_type = BaseType::kBaseTypeSigned;
      type_name = "int16_t";
      break;
    case VectorRegisterFormat::kUnsigned16:
      byte_size = 2;
      base_type = BaseType::kBaseTypeUnsigned;
      type_name = "uint16_t";
      break;
    case VectorRegisterFormat::kSigned32:
      byte_size = 4;
      base_type = BaseType::kBaseTypeSigned;
      type_name = "int32_t";
      break;
    case VectorRegisterFormat::kUnsigned32:
      byte_size = 4;
      base_type = BaseType::kBaseTypeUnsigned;
      type_name = "uint32_t";
      break;
    case VectorRegisterFormat::kSigned64:
      byte_size = 8;
      base_type = BaseType::kBaseTypeSigned;
      type_name = "int64_t";
      break;
    case VectorRegisterFormat::kUnsigned64:
      byte_size = 8;
      base_type = BaseType::kBaseTypeUnsigned;
      type_name = "uint64_t";
      break;
    case VectorRegisterFormat::kSigned128:
      byte_size = 16;
      base_type = BaseType::kBaseTypeSigned;
      type_name = "int128_t";
      break;
    case VectorRegisterFormat::kUnsigned128:
      byte_size = 16;
      base_type = BaseType::kBaseTypeUnsigned;
      type_name = "uint128_t";
      break;
    case VectorRegisterFormat::kFloat:
      byte_size = 4;
      base_type = BaseType::kBaseTypeFloat;
      type_name = "float";
      break;
    case VectorRegisterFormat::kDouble:
      byte_size = 8;
      base_type = BaseType::kBaseTypeFloat;
      type_name = "double";
      break;
  }

  auto item_type = fxl::MakeRefCounted<BaseType>(base_type, byte_size, type_name);

  size_t array_size = data.size() / byte_size;
  auto array_type = fxl::MakeRefCounted<ArrayType>(std::move(item_type), array_size);

  return ExprValue(std::move(array_type), std::move(data), ExprValueSource(id));
}

}  // namespace zxdb
