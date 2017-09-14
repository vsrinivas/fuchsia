// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Serialization warnings are only recorded in debug build.
#ifndef NDEBUG

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/compiler/interfaces/tests/serialization_test_structs.fidl.h"

namespace fidl {
namespace test {
namespace {

using fidl::internal::ArrayValidateParams;

// Creates an array of arrays of handles (2 X 3) for testing.
Array<Array<zx::handle>> CreateTestNestedHandleArray() {
  auto array = Array<Array<zx::handle>>::New(2);
  for (size_t i = 0; i < array.size(); ++i) {
    auto nested_array = Array<zx::handle>::New(3);
    for (size_t j = 0; j < nested_array.size(); ++j) {
      zx::channel handle0, handle1;
      zx::channel::create(0, &handle0, &handle1);
      nested_array[j] = zx::handle(std::move(handle1));
    }
    array[i] = std::move(nested_array);
  }

  return array;
}

class SerializationWarningTest : public testing::Test {
 public:
  ~SerializationWarningTest() override {}

 protected:
  template <typename T>
  void TestWarning(StructPtr<T> obj,
                   fidl::internal::ValidationError expected_warning) {
    TestStructWarningImpl<T>(std::move(obj), expected_warning);
  }

  template <typename T>
  void TestWarning(InlinedStructPtr<T> obj,
                   fidl::internal::ValidationError expected_warning) {
    TestStructWarningImpl<T>(std::move(obj), expected_warning);
  }

  template <typename T, typename TPtr>
  void TestStructWarningImpl(TPtr obj,
                             fidl::internal::ValidationError expected_warning) {
    fidl::internal::FixedBufferForTesting buf(GetSerializedSize_(*obj));
    typename T::Data_* data;
    EXPECT_EQ(expected_warning, Serialize_(obj.get(), &buf, &data));
  }

  template <typename T>
  void TestArrayWarning(T obj,
                        fidl::internal::ValidationError expected_warning,
                        const ArrayValidateParams* validate_params) {
    fidl::internal::FixedBufferForTesting buf(GetSerializedSize_(obj));
    typename T::Data_* data;
    EXPECT_EQ(expected_warning,
              SerializeArray_(&obj, &buf, &data, validate_params));
  }
};

TEST_F(SerializationWarningTest, HandleInStruct) {
  Struct2Ptr test_struct(Struct2::New());
  EXPECT_FALSE(test_struct->hdl);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE);

  test_struct = Struct2::New();
  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  test_struct->hdl = std::move(handle1);

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, StructInStruct) {
  Struct3Ptr test_struct(Struct3::New());
  EXPECT_TRUE(!test_struct->struct_1);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct3::New();
  test_struct->struct_1 = Struct1::New();

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, ArrayOfStructsInStruct) {
  Struct4Ptr test_struct(Struct4::New());
  EXPECT_TRUE(!test_struct->data);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct4::New();
  test_struct->data.resize(1);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct4::New();
  test_struct->data.resize(0);

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);

  test_struct = Struct4::New();
  test_struct->data.resize(1);
  test_struct->data[0] = Struct1::New();

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, FixedArrayOfStructsInStruct) {
  Struct5Ptr test_struct(Struct5::New());
  EXPECT_TRUE(!test_struct->pair);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct5::New();
  test_struct->pair.resize(1);
  test_struct->pair[0] = Struct1::New();

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_ARRAY_HEADER);

  test_struct = Struct5::New();
  test_struct->pair.resize(2);
  test_struct->pair[0] = Struct1::New();
  test_struct->pair[1] = Struct1::New();

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, StringInStruct) {
  Struct6Ptr test_struct(Struct6::New());
  EXPECT_TRUE(!test_struct->str);

  TestWarning(std::move(test_struct),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct6::New();
  test_struct->str = "hello world";

  TestWarning(std::move(test_struct), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, ArrayOfArraysOfHandles) {
  Array<Array<zx::handle>> test_array = CreateTestNestedHandleArray();
  test_array[0] = Array<zx::handle>();
  test_array[1][0] = zx::handle();

  ArrayValidateParams validate_params_0(
      0, true, new ArrayValidateParams(0, true, nullptr));
  TestArrayWarning(std::move(test_array), fidl::internal::ValidationError::NONE,
                   &validate_params_0);

  test_array = CreateTestNestedHandleArray();
  test_array[0] = Array<zx::handle>();
  ArrayValidateParams validate_params_1(
      0, false, new ArrayValidateParams(0, true, nullptr));
  TestArrayWarning(std::move(test_array),
                   fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
                   &validate_params_1);

  test_array = CreateTestNestedHandleArray();
  test_array[1][0] = zx::handle();
  ArrayValidateParams validate_params_2(
      0, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(std::move(test_array),
                   fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
                   &validate_params_2);
}

TEST_F(SerializationWarningTest, ArrayOfStrings) {
  auto test_array = Array<String>::New(3);
  for (size_t i = 0; i < test_array.size(); ++i)
    test_array[i] = "hello";

  ArrayValidateParams validate_params_0(
      0, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(std::move(test_array), fidl::internal::ValidationError::NONE,
                   &validate_params_0);

  test_array = Array<String>::New(3);
  ArrayValidateParams validate_params_1(
      0, false, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(std::move(test_array),
                   fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
                   &validate_params_1);

  test_array = Array<String>::New(2);
  ArrayValidateParams validate_params_2(
      3, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(std::move(test_array),
                   fidl::internal::ValidationError::UNEXPECTED_ARRAY_HEADER,
                   &validate_params_2);
}

}  // namespace
}  // namespace test
}  // namespace fidl

#endif
