// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vector_register_format.h"

#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

using debug_ipc::RegisterID;

const char* VectorRegisterFormatToString(VectorRegisterFormat fmt) {
  switch (fmt) {
    case VectorRegisterFormat::kSigned8:
      return "i8";
    case VectorRegisterFormat::kUnsigned8:
      return "u8";
    case VectorRegisterFormat::kSigned16:
      return "i16";
    case VectorRegisterFormat::kUnsigned16:
      return "u16";
    case VectorRegisterFormat::kSigned32:
      return "i32";
    case VectorRegisterFormat::kUnsigned32:
      return "u32";
    case VectorRegisterFormat::kSigned64:
      return "i64";
    case VectorRegisterFormat::kUnsigned64:
      return "u64";
    case VectorRegisterFormat::kSigned128:
      return "i128";
    case VectorRegisterFormat::kUnsigned128:
      return "u128";
    case VectorRegisterFormat::kFloat:
      return "float";
    case VectorRegisterFormat::kDouble:
      return "double";
  }
  FXL_NOTREACHED();
  return "";
}

ExprValue VectorRegisterToValue(VectorRegisterFormat fmt, std::vector<uint8_t> data) {
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

  return ExprValue(std::move(array_type), std::move(data));
}

bool ShouldFormatRegisterAsVector(RegisterID id_enum) {
  uint32_t id = static_cast<uint32_t>(id_enum);

  // ARM
  if (id >= static_cast<uint32_t>(RegisterID::kARMv8_v0) &&
      id <= static_cast<uint32_t>(RegisterID::kARMv8_v31))
    return true;

  // Old-style MMX.
  if (id >= static_cast<uint32_t>(RegisterID::kX64_mm0) &&
      id <= static_cast<uint32_t>(RegisterID::kX64_mm7))
    return true;

  // New-style x/y/zmm.
  if (id >= static_cast<uint32_t>(RegisterID::kX64_xmm0) &&
      id <= static_cast<uint32_t>(RegisterID::kX64_xmm31))
    return true;
  if (id >= static_cast<uint32_t>(RegisterID::kX64_ymm0) &&
      id <= static_cast<uint32_t>(RegisterID::kX64_ymm31))
    return true;
  if (id >= static_cast<uint32_t>(RegisterID::kX64_zmm0) &&
      id <= static_cast<uint32_t>(RegisterID::kX64_zmm31))
    return true;

  return false;
}

}  // namespace zxdb
