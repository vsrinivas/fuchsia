// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value.h"

#include <sstream>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

TEST(ExprValue, PromoteTo) {
  ExprValue byte_minus_one(static_cast<int8_t>(-1));

  // Unsigned promotion just copies the bits.
  uint64_t u64 = 0;
  EXPECT_TRUE(byte_minus_one.PromoteTo64(&u64).ok());
  EXPECT_EQ(255u, u64);

  uint128_t u128 = 0;
  EXPECT_TRUE(byte_minus_one.PromoteTo128(&u128).ok());
  EXPECT_EQ(255u, u128);

  // Signed conversion gets a sign expansion.
  int64_t i64 = 0;
  EXPECT_TRUE(byte_minus_one.PromoteTo64(&i64).ok());
  EXPECT_EQ(-1ll, i64);

  int128_t i128 = 0;
  EXPECT_TRUE(byte_minus_one.PromoteTo128(&i128).ok());
  EXPECT_EQ(-1ll, i128);

  // 5 byte value is an error.
  ExprValue five_bytes(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 5, "int40_t"),
                       {1, 1, 1, 1, 1});
  Err err = five_bytes.PromoteTo64(&i64);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ("Unexpected value size (5), please file a bug.", err.msg());

  // Empty value is an error.
  ExprValue empty;
  err = empty.PromoteTo64(&i64);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ("Value has no data.", err.msg());
}

TEST(ExprValue, StreamOut) {
  std::ostringstream out;
  out << ExprValue();
  EXPECT_EQ("{null ExprValue}", out.str());

  out.str("");
  out << ExprValue(static_cast<int8_t>(23), MakeSignedChar8Type());
  EXPECT_EQ("char(23)", out.str());

  out.str("");
  out << ExprValue(-19);
  EXPECT_EQ("int32_t(-19)", out.str());

  out.str("");
  out << ExprValue(3.14);
  EXPECT_EQ("double(3.140000)", out.str());

  // Make a collection with one element.
  auto int64_type = MakeInt64Type();
  auto collection = MakeCollectionType(DwarfTag::kClassType, "MyClass", {{"a", int64_type}});
  EXPECT_EQ(collection->byte_size(), 8u);
  std::vector<uint8_t> collection_data;
  for (size_t i = 0; i < 8; i++)
    collection_data.push_back(static_cast<uint8_t>(i));

  out.str("");
  out << ExprValue(collection, collection_data);
  EXPECT_EQ("MyClass(0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07)", out.str());
}

}  // namespace zxdb
