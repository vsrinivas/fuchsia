// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Serialization warnings are only recorded in debug build.
#ifndef NDEBUG

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/interfaces/bindings/tests/serialization_test_structs.mojom.h"

namespace fidl {
namespace test {
namespace {

using fidl::internal::ArrayValidateParams;

// Creates an array of arrays of handles (2 X 3) for testing.
Array<Array<ScopedHandle>> CreateTestNestedHandleArray() {
  auto array = Array<Array<ScopedHandle>>::New(2);
  for (size_t i = 0; i < array.size(); ++i) {
    auto nested_array = Array<ScopedHandle>::New(3);
    for (size_t j = 0; j < nested_array.size(); ++j) {
      MessagePipe pipe;
      nested_array[j] = ScopedHandle::From(pipe.handle1.Pass());
    }
    array[i] = nested_array.Pass();
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
    TestStructWarningImpl<T>(obj.Pass(), expected_warning);
  }

  template <typename T>
  void TestWarning(InlinedStructPtr<T> obj,
                   fidl::internal::ValidationError expected_warning) {
    TestStructWarningImpl<T>(obj.Pass(), expected_warning);
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
  EXPECT_FALSE(test_struct->hdl.is_valid());

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE);

  test_struct = Struct2::New();
  MessagePipe pipe;
  test_struct->hdl = ScopedHandle::From(pipe.handle1.Pass());

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, StructInStruct) {
  Struct3Ptr test_struct(Struct3::New());
  EXPECT_TRUE(!test_struct->struct_1);

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct3::New();
  test_struct->struct_1 = Struct1::New();

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, ArrayOfStructsInStruct) {
  Struct4Ptr test_struct(Struct4::New());
  EXPECT_TRUE(!test_struct->data);

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct4::New();
  test_struct->data.resize(1);

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct4::New();
  test_struct->data.resize(0);

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);

  test_struct = Struct4::New();
  test_struct->data.resize(1);
  test_struct->data[0] = Struct1::New();

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, FixedArrayOfStructsInStruct) {
  Struct5Ptr test_struct(Struct5::New());
  EXPECT_TRUE(!test_struct->pair);

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct5::New();
  test_struct->pair.resize(1);
  test_struct->pair[0] = Struct1::New();

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_ARRAY_HEADER);

  test_struct = Struct5::New();
  test_struct->pair.resize(2);
  test_struct->pair[0] = Struct1::New();
  test_struct->pair[1] = Struct1::New();

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, StringInStruct) {
  Struct6Ptr test_struct(Struct6::New());
  EXPECT_TRUE(!test_struct->str);

  TestWarning(test_struct.Pass(),
              fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER);

  test_struct = Struct6::New();
  test_struct->str = "hello world";

  TestWarning(test_struct.Pass(), fidl::internal::ValidationError::NONE);
}

TEST_F(SerializationWarningTest, ArrayOfArraysOfHandles) {
  Array<Array<ScopedHandle>> test_array = CreateTestNestedHandleArray();
  test_array[0] = Array<ScopedHandle>();
  test_array[1][0] = ScopedHandle();

  ArrayValidateParams validate_params_0(
      0, true, new ArrayValidateParams(0, true, nullptr));
  TestArrayWarning(test_array.Pass(), fidl::internal::ValidationError::NONE,
                   &validate_params_0);

  test_array = CreateTestNestedHandleArray();
  test_array[0] = Array<ScopedHandle>();
  ArrayValidateParams validate_params_1(
      0, false, new ArrayValidateParams(0, true, nullptr));
  TestArrayWarning(test_array.Pass(),
                   fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
                   &validate_params_1);

  test_array = CreateTestNestedHandleArray();
  test_array[1][0] = ScopedHandle();
  ArrayValidateParams validate_params_2(
      0, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(test_array.Pass(),
                   fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
                   &validate_params_2);
}

TEST_F(SerializationWarningTest, ArrayOfStrings) {
  auto test_array = Array<String>::New(3);
  for (size_t i = 0; i < test_array.size(); ++i)
    test_array[i] = "hello";

  ArrayValidateParams validate_params_0(
      0, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(test_array.Pass(), fidl::internal::ValidationError::NONE,
                   &validate_params_0);

  test_array = Array<String>::New(3);
  ArrayValidateParams validate_params_1(
      0, false, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(test_array.Pass(),
                   fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
                   &validate_params_1);

  test_array = Array<String>::New(2);
  ArrayValidateParams validate_params_2(
      3, true, new ArrayValidateParams(0, false, nullptr));
  TestArrayWarning(test_array.Pass(),
                   fidl::internal::ValidationError::UNEXPECTED_ARRAY_HEADER,
                   &validate_params_2);
}

}  // namespace
}  // namespace test
}  // namespace fidl

#endif
