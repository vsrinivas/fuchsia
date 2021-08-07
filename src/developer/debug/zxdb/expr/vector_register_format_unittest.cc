// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vector_register_format.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

namespace {

using debug::RegisterID;

// Returns true if the expr value type matches the parameters.
bool IsTypeArrayOf(const ExprValue& value, const char* expected_type, uint32_t expected_bytes,
                   size_t expected_array_elts) {
  const Type* type = value.type();
  if (!type)
    return false;

  const ArrayType* array = type->As<ArrayType>();
  if (!array)
    return false;

  const Type* array_value = array->value_type();
  return array_value->GetFullName() == expected_type &&
         array_value->byte_size() == expected_bytes && array->num_elts() == expected_array_elts;
}

}  // namespace

TEST(VectorRegisterFormat, VectorRegisterToValue) {
  std::vector<uint8_t> bytes{1, 2, 3, 4, 5, 6, 7, 8};
  ExprValue v =
      VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kUnsigned16, bytes);
  EXPECT_TRUE(IsTypeArrayOf(v, "uint16_t", 2u, 4u));
  EXPECT_EQ(bytes, v.data().bytes());
  EXPECT_EQ(ExprValueSource::Type::kRegister, v.source().type());
  EXPECT_EQ(RegisterID::kX64_xmm4, v.source().register_id());

  v = VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kSigned8, bytes);
  EXPECT_TRUE(IsTypeArrayOf(v, "int8_t", 1u, 8u));

  v = VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kUnsigned16, bytes);
  EXPECT_TRUE(IsTypeArrayOf(v, "uint16_t", 2u, 4u));

  v = VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kUnsigned128,
                            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_TRUE(IsTypeArrayOf(v, "uint128_t", 16u, 1u));

  v = VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kFloat, bytes);
  EXPECT_TRUE(IsTypeArrayOf(v, "float", 4u, 2u));

  v = VectorRegisterToValue(RegisterID::kX64_xmm4, VectorRegisterFormat::kDouble, bytes);
  EXPECT_TRUE(IsTypeArrayOf(v, "double", 8u, 1u));
}

}  // namespace zxdb
