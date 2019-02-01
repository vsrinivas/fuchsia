// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/array.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fsl/types/type_converters.h"

namespace {
struct MyInteger {
  MyInteger() = default;
  explicit MyInteger(int x) : i(x) {}
  int i;
};

static inline bool operator==(const MyInteger& lhs, const MyInteger& rhs) {
  return lhs.i == rhs.i;
}
}  // namespace

namespace fxl {
template <>
struct TypeConverter<int, MyInteger> {
  static int Convert(const MyInteger& value) { return value.i; }
};

template <>
struct TypeConverter<MyInteger, int> {
  static MyInteger Convert(const int& value) { return MyInteger(value); }
};
}  // namespace fxl

namespace fsl {
namespace {

TEST(TypeConversionTest, Vector) {
  std::vector<int> vec = {1, 2, 3};
  fidl::VectorPtr<int> vecptr = fxl::To<fidl::VectorPtr<int>>(vec);

  EXPECT_FALSE(vecptr.is_null());
  EXPECT_TRUE(vecptr);
  EXPECT_EQ(vec, *vecptr);

  std::vector<int> vec2 = fxl::To<std::vector<int>>(vecptr);
  EXPECT_EQ(vec, vec2);
}

TEST(TypeConversionTest, Vector_DifferentTypes) {
  std::vector<MyInteger> vec;
  vec.push_back(MyInteger(1));
  vec.push_back(MyInteger(2));
  vec.push_back(MyInteger(3));

  fidl::VectorPtr<int> vecptr = fxl::To<fidl::VectorPtr<int>>(vec);
  EXPECT_FALSE(vecptr.is_null());
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(fxl::To<int>(vec[i]), (*vecptr)[i]);
  }

  std::vector<MyInteger> vec2 = fxl::To<std::vector<MyInteger>>(vecptr);
  EXPECT_EQ(vec, vec2);
}

TEST(TypeConversionTest, Vector_Null) {
  fidl::VectorPtr<int> vecptr;
  std::vector<int> vec = fxl::To<std::vector<int>>(vecptr);
  EXPECT_TRUE(vec.empty());
}

TEST(TypeConversionTest, Array_Vector) {
  constexpr size_t kSize = 3;
  const int kOriginal[kSize] = {1, 2, 3};
  fidl::Array<int, kSize> array;
  for (size_t i = 0; i < array.count(); ++i) {
    array[i] = kOriginal[i];
  }

  fidl::VectorPtr<int> vecptr = fxl::To<fidl::VectorPtr<int>>(array);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(kOriginal[i], (*vecptr)[i]);
  }

  std::vector<int> vec = fxl::To<std::vector<int>>(array);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(kOriginal[i], vec[i]);
  }
}

TEST(TypeConversionTest, Array_Vector_DifferentTypes) {
  constexpr size_t kSize = 3;
  const int kOriginal[kSize] = {1, 2, 3};
  fidl::Array<int, kSize> array;
  for (size_t i = 0; i < array.count(); ++i) {
    array[i] = kOriginal[i];
  }

  fidl::VectorPtr<MyInteger> vecptr =
      fxl::To<fidl::VectorPtr<MyInteger>>(array);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(MyInteger(kOriginal[i]), (*vecptr)[i]);
  }

  std::vector<MyInteger> vec = fxl::To<std::vector<MyInteger>>(array);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(MyInteger(kOriginal[i]), vec[i]);
  }
}

TEST(TypeConversionTest, String) {
  std::string str = "hello world";
  fidl::StringPtr strptr = fxl::To<fidl::StringPtr>(str);
  EXPECT_FALSE(strptr.is_null());
  EXPECT_EQ(str, *strptr);

  std::string str2 = fxl::To<std::string>(strptr);
  EXPECT_EQ(str, str2);
}

TEST(TypeConversionTest, String_Null) {
  fidl::StringPtr strptr;
  std::string str = fxl::To<std::string>(strptr);
  EXPECT_EQ("", str);
}

}  // namespace
}  // namespace fsl
