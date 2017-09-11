// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/tests/util/container_test_util.h"
#include "lib/fidl/cpp/bindings/tests/util/iterator_test_util.h"
#include "lib/fidl/compiler/interfaces/tests/test_arrays.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/test_structs.fidl.h"
#include "lib/fxl/arraysize.h"

namespace fidl {
namespace test {
namespace {

using fidl::internal::Array_Data;
using fidl::internal::ArrayValidateParams;
using fidl::internal::FixedBufferForTesting;
using fidl::internal::String_Data;

// Tests that basic Array operations work.
TEST(ArrayTest, Basic) {
  auto array = Array<uint8_t>::New(8);
  for (size_t i = 0u; i < array.size(); ++i) {
    uint8_t val = static_cast<uint8_t>(i * 2);
    array[i] = val;
    EXPECT_EQ(val, array.at(i));
  }

  EXPECT_EQ(0u, *array.data());
  EXPECT_EQ(2u, *(array.data() + 1));
  EXPECT_EQ(4u, *(array.data() + 2));
}

TEST(ArrayTest, Testability) {
  Array<int32_t> array;
  EXPECT_FALSE(array);
  EXPECT_TRUE(array.is_null());

  array.push_back(123);
  EXPECT_TRUE(array);
  EXPECT_FALSE(array.is_null());
}

void NullptrConstructorTestHelper(Array<int32_t> array) {
  EXPECT_FALSE(array);
  EXPECT_TRUE(array.is_null());
  EXPECT_EQ(0u, array.size());
}

TEST(ArrayTest, NullptrConstructor) {
  Array<int32_t> array(nullptr);
  EXPECT_FALSE(array);
  EXPECT_TRUE(array.is_null());
  EXPECT_EQ(0u, array.size());

  array.push_back(123);
  EXPECT_TRUE(array);
  EXPECT_FALSE(array.is_null());
  EXPECT_EQ(1u, array.size());

  // Test some implicit constructions of |Array<int32_t>| from a |nullptr|.
  array = nullptr;
  NullptrConstructorTestHelper(nullptr);
}

// Tests that basic Array<bool> operations work.
TEST(ArrayTest, Bool) {
  auto array = Array<bool>::New(64);
  for (size_t i = 0; i < array.size(); ++i) {
    bool val = i % 3 == 0;
    array[i] = val;
    EXPECT_EQ(val, array.at(i));
  }
}


// Tests that Array<mx::channel> supports transferring handles.
TEST(ArrayTest, Handle) {
  mx::channel handle0;
  mx::channel handle1;
  mx::channel::create(0, &handle0, &handle1);
  
  auto handles = Array<mx::channel>::New(2);
  handles[0] = std::move(handle0);
  handles[1].reset(handle1.release());

  EXPECT_FALSE(handle0);
  EXPECT_FALSE(handle1);

  Array<mx::channel> handles2 = std::move(handles);
  EXPECT_TRUE(handles2[0]);
  EXPECT_TRUE(handles2[1]);

  mx::channel pipe_handle = std::move(handles2[0]);
  EXPECT_TRUE(pipe_handle);
  EXPECT_FALSE(handles2[0]);
}

// Tests that Array<mx::channel> supports closing handles.
TEST(ArrayTest, HandlesAreClosed) {
  mx::channel handle0;
  mx::channel handle1;
  mx::channel::create(0, &handle0, &handle1);
  mx_handle_t handle0_val = handle0.get();

  {
    auto handles = Array<mx::channel>::New(2);
    handles[0] = std::move(handle0);
    handles[1].reset(handle1.get());
  }

  // We expect the pipes to have been closed.
  EXPECT_EQ(MX_ERR_BAD_HANDLE, mx_handle_close(handle0_val));
  EXPECT_EQ(MX_ERR_BAD_HANDLE, mx_handle_close(handle1.get()));
}

TEST(ArrayTest, Clone) {
  {
    // Test POD.
    auto array = Array<int32_t>::New(3);
    for (size_t i = 0; i < array.size(); ++i)
      array[i] = static_cast<int32_t>(i);

    Array<int32_t> clone_array = array.Clone();
    EXPECT_EQ(array.size(), clone_array.size());
    for (size_t i = 0; i < array.size(); ++i)
      EXPECT_EQ(array[i], clone_array[i]);
  }

  {
    // Test copyable object.
    auto array = Array<String>::New(2);
    array[0] = "hello";
    array[1] = "world";

    Array<String> clone_array = array.Clone();
    EXPECT_EQ(array.size(), clone_array.size());
    for (size_t i = 0; i < array.size(); ++i)
      EXPECT_EQ(array[i], clone_array[i]);
  }

  {
    // Test struct.
    auto array = Array<RectPtr>::New(2);
    array[1] = Rect::New();
    array[1]->x = 1;
    array[1]->y = 2;
    array[1]->width = 3;
    array[1]->height = 4;

    Array<RectPtr> clone_array = array.Clone();
    EXPECT_EQ(array.size(), clone_array.size());
    EXPECT_TRUE(clone_array[0].is_null());
    EXPECT_EQ(array[1]->x, clone_array[1]->x);
    EXPECT_EQ(array[1]->y, clone_array[1]->y);
    EXPECT_EQ(array[1]->width, clone_array[1]->width);
    EXPECT_EQ(array[1]->height, clone_array[1]->height);
  }

  {
    // Test array of array.
    auto array = Array<Array<int8_t>>::New(2);
    array[1] = Array<int8_t>::New(2);
    array[1][0] = 0;
    array[1][1] = 1;

    Array<Array<int8_t>> clone_array = array.Clone();
    EXPECT_EQ(array.size(), clone_array.size());
    EXPECT_TRUE(clone_array[0].is_null());
    EXPECT_EQ(array[1].size(), clone_array[1].size());
    EXPECT_EQ(array[1][0], clone_array[1][0]);
    EXPECT_EQ(array[1][1], clone_array[1][1]);
  }

  {
    // Test that array of handles still works although Clone() is not available.
    auto array = Array<mx::channel>::New(10);
    EXPECT_FALSE(array[0]);
  }
}

TEST(ArrayTest, Serialization_ArrayOfPOD) {
  auto array = Array<int32_t>::New(4);
  for (size_t i = 0; i < array.size(); ++i)
    array[i] = static_cast<int32_t>(i);

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U + 4 * 4U, size);

  FixedBufferForTesting buf(size);
  Array_Data<int32_t>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  Array<int32_t> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(4U, array2.size());
  for (size_t i = 0; i < array2.size(); ++i)
    EXPECT_EQ(static_cast<int32_t>(i), array2[i]);
}

TEST(ArrayTest, Serialization_EmptyArrayOfPOD) {
  auto array = Array<int32_t>::New(0);
  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U, size);

  FixedBufferForTesting buf(size);
  Array_Data<int32_t>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  Array<int32_t> array2;
  Deserialize_(data, &array2);
  EXPECT_EQ(0U, array2.size());
}

TEST(ArrayTest, Serialization_ArrayOfArrayOfPOD) {
  auto array = Array<Array<int32_t>>::New(2);
  for (size_t j = 0; j < array.size(); ++j) {
    auto inner = Array<int32_t>::New(4);
    for (size_t i = 0; i < inner.size(); ++i)
      inner[i] = static_cast<int32_t>(i + (j * 10));
    array[j] = std::move(inner);
  }

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U + 2 * 8U + 2 * (8U + 4 * 4U), size);

  FixedBufferForTesting buf(size);
  Array_Data<Array_Data<int32_t>*>* data = nullptr;
  ArrayValidateParams validate_params(
      0, false, new ArrayValidateParams(0, false, nullptr));
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  Array<Array<int32_t>> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(2U, array2.size());
  for (size_t j = 0; j < array2.size(); ++j) {
    const Array<int32_t>& inner = array2[j];
    EXPECT_EQ(4U, inner.size());
    for (size_t i = 0; i < inner.size(); ++i)
      EXPECT_EQ(static_cast<int32_t>(i + (j * 10)), inner[i]);
  }
}

TEST(ArrayTest, Serialization_ArrayOfScopedEnum) {
  enum class TestEnum : int32_t {
    E0,
    E1,
    E2,
    E3,
  };
  static const TestEnum TEST_VALS[] = {
      TestEnum::E0, TestEnum::E2, TestEnum::E1, TestEnum::E3,
      TestEnum::E2, TestEnum::E2, TestEnum::E2, TestEnum::E0,
  };

  auto array = Array<TestEnum>::New(arraysize(TEST_VALS));
  for (size_t i = 0; i < array.size(); ++i)
    array[i] = TEST_VALS[i];

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U + (arraysize(TEST_VALS) * sizeof(int32_t)), size);

  FixedBufferForTesting buf(size);
  Array_Data<int32_t>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);
  SerializeArray_(&array, &buf, &data, &validate_params);

  Array<TestEnum> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(arraysize(TEST_VALS), array2.size());
  for (size_t i = 0; i < array2.size(); ++i)
    EXPECT_EQ(TEST_VALS[i], array2[i]);
}

TEST(ArrayTest, Serialization_ArrayOfBool) {
  auto array = Array<bool>::New(10);
  for (size_t i = 0; i < array.size(); ++i)
    array[i] = i % 2 ? true : false;

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U + 8U, size);

  FixedBufferForTesting buf(size);
  Array_Data<bool>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  Array<bool> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(10U, array2.size());
  for (size_t i = 0; i < array2.size(); ++i)
    EXPECT_EQ(i % 2 ? true : false, array2[i]);
}

TEST(ArrayTest, Serialization_ArrayOfString) {
  auto array = Array<String>::New(10);
  for (size_t i = 0; i < array.size(); ++i) {
    char c = 'A' + static_cast<char>(i);
    array[i] = String(&c, 1);
  }

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U +            // array header
                10 * 8U +   // array payload (10 pointers)
                10 * (8U +  // string header
                      8U),  // string length of 1 padded to 8
            size);

  FixedBufferForTesting buf(size);
  Array_Data<String_Data*>* data = nullptr;
  ArrayValidateParams validate_params(
      0, false, new ArrayValidateParams(0, false, nullptr));
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  Array<String> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(10U, array2.size());
  for (size_t i = 0; i < array2.size(); ++i) {
    char c = 'A' + static_cast<char>(i);
    EXPECT_EQ(String(&c, 1), array2[i]);
  }
}

// Tests serializing and deserializing an Array<Handle>.
TEST(ArrayTest, Serialization_ArrayOfHandle) {
  auto array = Array<mx::handle>::New(4);
  mx::channel p0_h0, p0_h1;
  mx::channel p1_h0, p1_h1;
  mx::channel::create(0, &p0_h0, &p0_h1);
  mx::channel::create(0, &p1_h0, &p1_h1);

  // array[0] is left invalid.
  array[1] = std::move(p0_h1);
  array[2] = std::move(p1_h0);
  array[3] = std::move(p1_h1);

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(8U               // array header
                + (4U * 4),  // 4 handles
            size);

  // We're going to reuse this buffer.. twice.
  FixedBufferForTesting buf(size * 3);
  Array_Data<fidl::internal::WrappedHandle>* data = nullptr;

  // 1.  Serialization should fail on non-nullable invalid Handle.
  ArrayValidateParams validate_params(4, false, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
            SerializeArray_(&array, &buf, &data, &validate_params));

  // We failed trying to transfer the first handle, so the rest are left valid.
  EXPECT_FALSE(array[0]);
  EXPECT_TRUE(array[1]);
  EXPECT_TRUE(array[2]);
  EXPECT_TRUE(array[3]);

  // 2.  Serialization should pass on nullable invalid Handle.
  ArrayValidateParams validate_params_nullable(4, true, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&array, &buf, &data, &validate_params_nullable));

  EXPECT_FALSE(array[0]);
  EXPECT_FALSE(array[1]);
  EXPECT_FALSE(array[2]);
  EXPECT_FALSE(array[3]);

  Deserialize_(data, &array);
  EXPECT_FALSE(array[0]);
  EXPECT_TRUE(array[1]);
  EXPECT_TRUE(array[2]);
  EXPECT_TRUE(array[3]);
}

// Test serializing and deserializing an Array<InterfacePtr>.
TEST(ArrayTest, Serialization_ArrayOfInterfacePtr) {
  auto iface_array = Array<fidl::InterfaceHandle<TestInterface>>::New(1);
  size_t size = GetSerializedSize_(iface_array);
  EXPECT_EQ(8U               // array header
                + (8U * 1),  // Interface_Data * number of elements
            size);

  FixedBufferForTesting buf(size * 3);
  Array_Data<fidl::internal::Interface_Data>* output = nullptr;

  // 1.  Invalid InterfacePtr should fail serialization.
  ArrayValidateParams validate_non_nullable(1, false, nullptr);
  EXPECT_EQ(
      fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
      SerializeArray_(&iface_array, &buf, &output, &validate_non_nullable));
  EXPECT_FALSE(iface_array[0]);

  // 2.  Invalid InterfacePtr should pass if array elements are nullable.
  ArrayValidateParams validate_nullable(1, true, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeArray_(&iface_array, &buf, &output, &validate_nullable));
  EXPECT_FALSE(iface_array[0]);

  // 3.  Should serialize successfully if InterfacePtr is valid.
  TestInterfacePtr iface_ptr;
  auto iface_req = iface_ptr.NewRequest();

  iface_array[0] = std::move(iface_ptr);
  EXPECT_TRUE(iface_array[0]);

  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      SerializeArray_(&iface_array, &buf, &output, &validate_non_nullable));
  EXPECT_FALSE(iface_array[0]);

  Deserialize_(output, &iface_array);
  EXPECT_TRUE(iface_array[0]);
}

// Test serializing and deserializing a struct with an Array<> of another struct
// which has an InterfacePtr.
TEST(ArrayTest, Serialization_StructWithArrayOfInterfacePtr) {
  StructWithInterfaceArray struct_arr_iface;
  struct_arr_iface.structs_array = Array<StructWithInterfacePtr>::New(1);
  struct_arr_iface.nullable_structs_array =
      Array<StructWithInterfacePtr>::New(1);
  struct_arr_iface.iface_array = Array<InterfaceHandle<TestInterface>>::New(0);
  struct_arr_iface.req_iface_array =
      Array<InterfaceRequest<TestInterface>>::New(0);
  struct_arr_iface.nullable_iface_array =
      Array<InterfaceHandle<TestInterface>>::New(0);
  struct_arr_iface.req_nullable_iface_array =
      Array<InterfaceRequest<TestInterface>>::New(0);

  size_t size = GetSerializedSize_(struct_arr_iface);
  EXPECT_EQ(8U            // struct header
                + 8U      // offset to |iface_array|
                + 8U      // offset to |nullable_iface_array|
                + 8U      // offset to |req_iface_array|
                + 8U      // offset to |req_nullable_iface_array|
                + 8U      // array header for |iface_array|.
                + 8U      // array header for |nullable_iface_array|.
                + 8U      // array header for |req_iface_array|.
                + 8U      // array header for |req_nullable_iface_array|.
                + 8U      // offset to |structs_array|
                + (8U     // array header
                   + 8U)  // offset to StructWithInterface (nullptr)
                + 8U      // offset to |structs_nullable_array|
                + 8U      // offset to |nullable_structs_array|
                + (8U     // array header
                   + 8U)  // offset to StructWithinInterface (nullptr)
                + 8U,     // offset to |nullable_structs_nullable_array|
            size);

  FixedBufferForTesting buf(size * 2);
  StructWithInterfaceArray::Data_* struct_arr_iface_data = nullptr;
  //  1. This should fail because |structs_array| has an invalid InterfacePtr<>
  //     and it is not nullable.
  EXPECT_EQ(fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
            Serialize_(&struct_arr_iface, &buf, &struct_arr_iface_data));

  //  2. Adding in a struct with a valid InterfacePtr<> will let it serialize.
  TestInterfacePtr iface_ptr;
  auto iface_req = iface_ptr.NewRequest();

  StructWithInterfacePtr iface_struct(StructWithInterface::New());
  iface_struct->iptr = std::move(iface_ptr);

  struct_arr_iface.structs_array[0] = std::move(iface_struct);
  ASSERT_TRUE(struct_arr_iface.structs_array[0]->iptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(&struct_arr_iface, &buf, &struct_arr_iface_data));

  EXPECT_FALSE(struct_arr_iface.structs_array[0]->iptr);

  Deserialize_(struct_arr_iface_data, &struct_arr_iface);
  EXPECT_TRUE(struct_arr_iface.structs_array[0]->iptr);
}

// Test serializing and deserializing a struct with an Array<> of interface
// requests.
TEST(ArrayTest, Serialization_StructWithArrayOfIntefaceRequest) {
  StructWithInterfaceRequests struct_arr_iface_req;
  struct_arr_iface_req.req_array =
      Array<InterfaceRequest<TestInterface>>::New(1);
  struct_arr_iface_req.nullable_req_array =
      Array<InterfaceRequest<TestInterface>>::New(1);

  size_t size = GetSerializedSize_(struct_arr_iface_req);
  EXPECT_EQ(8U            // struct header
                + 8U      // offset to |req_array|
                + (8U     // array header for |req_array|
                   + 4U   // InterfaceRequest
                   + 4U)  // alignment padding
                + 8U      // offset to |req_nullable_array|
                + 8U      // offset to |nullable_req_array|
                + (8U     // array header for |nullable_req_array|
                   + 4U   // InterfaceRequest
                   + 4U)  // alignment padding
                + 8U,     // offset to |nullable_req_nullable_array|
            size);

  FixedBufferForTesting buf(size * 2);
  StructWithInterfaceRequests::Data_* struct_arr_iface_req_data;
  //  1. This should fail because |req_array| has an invalid InterfaceRequest<>
  //     and it is not nullable.
  EXPECT_EQ(
      fidl::internal::ValidationError::UNEXPECTED_INVALID_HANDLE,
      Serialize_(&struct_arr_iface_req, &buf, &struct_arr_iface_req_data));

  //  2. Adding in a valid InterfacePtr<> will let it serialize.
  TestInterfacePtr iface_ptr;
  struct_arr_iface_req.req_array[0] = iface_ptr.NewRequest();
  EXPECT_TRUE(struct_arr_iface_req.req_array[0].is_pending());

  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      Serialize_(&struct_arr_iface_req, &buf, &struct_arr_iface_req_data));

  EXPECT_FALSE(struct_arr_iface_req.req_array[0].is_pending());

  Deserialize_(struct_arr_iface_req_data, &struct_arr_iface_req);
  EXPECT_TRUE(struct_arr_iface_req.req_array[0].is_pending());
}

TEST(ArrayTest, Resize_Copyable) {
  ASSERT_EQ(0u, CopyableType::num_instances());
  auto array = fidl::Array<CopyableType>::New(3);
  std::vector<CopyableType*> value_ptrs;
  value_ptrs.push_back(array[0].ptr());
  value_ptrs.push_back(array[1].ptr());

  for (size_t i = 0; i < array.size(); i++)
    array[i].ResetCopied();

  array.resize(2);
  ASSERT_EQ(2u, array.size());
  EXPECT_EQ(array.size(), CopyableType::num_instances());
  for (size_t i = 0; i < array.size(); i++) {
    EXPECT_FALSE(array[i].copied());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }

  array.resize(3);
  array[2].ResetCopied();
  ASSERT_EQ(3u, array.size());
  EXPECT_EQ(array.size(), CopyableType::num_instances());
  for (size_t i = 0; i < array.size(); i++)
    EXPECT_FALSE(array[i].copied());
  value_ptrs.push_back(array[2].ptr());

  size_t capacity = array.storage().capacity();
  array.resize(capacity);
  ASSERT_EQ(capacity, array.size());
  EXPECT_EQ(array.size(), CopyableType::num_instances());
  for (size_t i = 0; i < 3; i++)
    EXPECT_FALSE(array[i].copied());
  for (size_t i = 3; i < array.size(); i++) {
    array[i].ResetCopied();
    value_ptrs.push_back(array[i].ptr());
  }

  array.resize(capacity + 2);
  ASSERT_EQ(capacity + 2, array.size());
  EXPECT_EQ(array.size(), CopyableType::num_instances());
  for (size_t i = 0; i < capacity; i++) {
    EXPECT_TRUE(array[i].copied());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }
  array.reset();
  EXPECT_EQ(0u, CopyableType::num_instances());
  EXPECT_FALSE(array);
  array.resize(0);
  EXPECT_EQ(0u, CopyableType::num_instances());
  EXPECT_TRUE(array);
}

TEST(ArrayTest, Resize_MoveOnly) {
  ASSERT_EQ(0u, MoveOnlyType::num_instances());
  auto array = fidl::Array<MoveOnlyType>::New(3);
  std::vector<MoveOnlyType*> value_ptrs;
  value_ptrs.push_back(array[0].ptr());
  value_ptrs.push_back(array[1].ptr());

  for (size_t i = 0; i < array.size(); i++)
    EXPECT_FALSE(array[i].moved());

  array.resize(2);
  ASSERT_EQ(2u, array.size());
  EXPECT_EQ(array.size(), MoveOnlyType::num_instances());
  for (size_t i = 0; i < array.size(); i++) {
    EXPECT_FALSE(array[i].moved());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }

  array.resize(3);
  ASSERT_EQ(3u, array.size());
  EXPECT_EQ(array.size(), MoveOnlyType::num_instances());
  for (size_t i = 0; i < array.size(); i++)
    EXPECT_FALSE(array[i].moved());
  value_ptrs.push_back(array[2].ptr());

  size_t capacity = array.storage().capacity();
  array.resize(capacity);
  ASSERT_EQ(capacity, array.size());
  EXPECT_EQ(array.size(), MoveOnlyType::num_instances());
  for (size_t i = 0; i < array.size(); i++)
    EXPECT_FALSE(array[i].moved());
  for (size_t i = 3; i < array.size(); i++)
    value_ptrs.push_back(array[i].ptr());

  array.resize(capacity + 2);
  ASSERT_EQ(capacity + 2, array.size());
  EXPECT_EQ(array.size(), MoveOnlyType::num_instances());
  for (size_t i = 0; i < capacity; i++) {
    EXPECT_TRUE(array[i].moved());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }
  for (size_t i = capacity; i < array.size(); i++)
    EXPECT_FALSE(array[i].moved());

  array.reset();
  EXPECT_EQ(0u, MoveOnlyType::num_instances());
  EXPECT_FALSE(array);
  array.resize(0);
  EXPECT_EQ(0u, MoveOnlyType::num_instances());
  EXPECT_TRUE(array);
}

TEST(ArrayTest, PushBack_Copyable) {
  ASSERT_EQ(0u, CopyableType::num_instances());
  auto array = fidl::Array<CopyableType>::New(2);
  array.reset();
  std::vector<CopyableType*> value_ptrs;
  size_t capacity = array.storage().capacity();
  for (size_t i = 0; i < capacity; i++) {
    CopyableType value;
    value_ptrs.push_back(value.ptr());
    array.push_back(value);
    ASSERT_EQ(i + 1, array.size());
    ASSERT_EQ(i + 1, value_ptrs.size());
    EXPECT_EQ(array.size() + 1, CopyableType::num_instances());
    EXPECT_TRUE(array[i].copied());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
    array[i].ResetCopied();
    EXPECT_TRUE(array);
  }
  {
    CopyableType value;
    value_ptrs.push_back(value.ptr());
    array.push_back(value);
    EXPECT_EQ(array.size() + 1, CopyableType::num_instances());
  }
  ASSERT_EQ(capacity + 1, array.size());
  EXPECT_EQ(array.size(), CopyableType::num_instances());

  for (size_t i = 0; i < array.size(); i++) {
    EXPECT_TRUE(array[i].copied());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }
  array.reset();
  EXPECT_EQ(0u, CopyableType::num_instances());
}

TEST(ArrayTest, PushBack_MoveOnly) {
  ASSERT_EQ(0u, MoveOnlyType::num_instances());
  auto array = fidl::Array<MoveOnlyType>::New(2);
  array.reset();
  std::vector<MoveOnlyType*> value_ptrs;
  size_t capacity = array.storage().capacity();
  for (size_t i = 0; i < capacity; i++) {
    MoveOnlyType value;
    value_ptrs.push_back(value.ptr());
    array.push_back(std::move(value));
    ASSERT_EQ(i + 1, array.size());
    ASSERT_EQ(i + 1, value_ptrs.size());
    EXPECT_EQ(array.size() + 1, MoveOnlyType::num_instances());
    EXPECT_TRUE(array[i].moved());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
    array[i].ResetMoved();
    EXPECT_TRUE(array);
  }
  {
    MoveOnlyType value;
    value_ptrs.push_back(value.ptr());
    array.push_back(std::move(value));
    EXPECT_EQ(array.size() + 1, MoveOnlyType::num_instances());
  }
  ASSERT_EQ(capacity + 1, array.size());
  EXPECT_EQ(array.size(), MoveOnlyType::num_instances());

  for (size_t i = 0; i < array.size(); i++) {
    EXPECT_TRUE(array[i].moved());
    EXPECT_EQ(value_ptrs[i], array[i].ptr());
  }
  array.reset();
  EXPECT_EQ(0u, MoveOnlyType::num_instances());
}

TEST(ArrayTest, Iterator) {
  std::vector<int> values;
  values.push_back(0);
  values.push_back(1);
  values.push_back(2);
  values.push_back(3);
  Array<int> arr = Array<int>::From(values);

  // Test RandomAcessIterator traits.
  {
    // Test +,-,+=,-=.
    auto i1 = arr.begin();
    i1 += 2;
    EXPECT_EQ(*i1, values[2]);
    i1 -= 2;
    EXPECT_EQ(*i1, values[0]);
    EXPECT_EQ((i1 + 2)[1], values[3]);

    auto i2 = arr.begin() + 3;
    EXPECT_EQ(*i2, values[3]);
    EXPECT_EQ(*(i2 - 2), values[1]);
    EXPECT_EQ(i2 - i1, 3);

    {
      auto j1 = arr.begin();
      auto j1_cp = arr.begin();
      j1 += 1;
      j1_cp++;
      EXPECT_EQ(j1, j1_cp);

      j1 -= 1;
      j1_cp--;
      EXPECT_EQ(j1, j1_cp);
    }

    // Test >, <, >=, <=.
    EXPECT_GT(i2, i1);
    EXPECT_LT(i1, i2);
    EXPECT_GE(i2, i1);
    EXPECT_LE(i1, i2);
  }

  {
    SCOPED_TRACE("Array iterator bidirectionality test.");
    ExpectBidiIteratorConcept(arr.begin(), arr.end(), values);
    ExpectBidiMutableIteratorConcept(arr.begin(), arr.end(), values);
  }
}

// Test serializing and deserializing of an array with null elements.
TEST(ArrayTest, Serialization_ArrayOfStructPtr) {
  ArrayValidateParams validate_nullable(2, true, nullptr);
  ArrayValidateParams validate_non_nullable(2, false, nullptr);

  Array<RectPtr> array = Array<RectPtr>::New(2);
  array[1] = Rect::New();
  array[1]->x = 1;
  array[1]->y = 2;
  array[1]->width = 3;
  array[1]->height = 4;

  size_t size_with_null = GetSerializedSize_(array);
  EXPECT_EQ(8U +              // array header
                2 * 8U +      // array payload (2 pointers)
                8U + 4 * 4U,  // struct header + contents (4 int32)
            size_with_null);
  Array_Data<Rect::Data_*>* output_with_null = nullptr;

  // 1. Array with non-nullable structs should fail serialization due to
  // the null first element.
  {
    FixedBufferForTesting buf_with_null(size_with_null);
    EXPECT_EQ(fidl::internal::ValidationError::UNEXPECTED_NULL_POINTER,
              SerializeArray_(&array, &buf_with_null, &output_with_null,
                              &validate_non_nullable));
  }

  // 2. Array with nullable structs should succeed.
  {
    FixedBufferForTesting buf_with_null(size_with_null);
    EXPECT_EQ(fidl::internal::ValidationError::NONE,
              SerializeArray_(&array, &buf_with_null, &output_with_null,
                              &validate_nullable));

    Array<RectPtr> array2;
    Deserialize_(output_with_null, &array2);
    EXPECT_TRUE(array2[0].is_null());
    EXPECT_FALSE(array2[1].is_null());
    EXPECT_EQ(1, array2[1]->x);
    EXPECT_EQ(2, array2[1]->y);
    EXPECT_EQ(3, array2[1]->width);
    EXPECT_EQ(4, array2[1]->height);
  }

  // 3. Array with non-nullable structs should succeed after we fill in
  // the missing first element.
  {
    array[0] = Rect::New();
    array[0]->x = -1;
    array[0]->y = -2;
    array[0]->width = -3;
    array[0]->height = -4;

    size_t size_without_null = GetSerializedSize_(array);
    EXPECT_EQ(8U +                    // array header
                  2 * 8U +            // array payload (2 pointers)
                  2 * (8U + 4 * 4U),  // struct header + contents (4 int32)
              size_without_null);

    FixedBufferForTesting buf_without_null(size_without_null);
    Array_Data<Rect::Data_*>* output_without_null = nullptr;
    EXPECT_EQ(fidl::internal::ValidationError::NONE,
              SerializeArray_(&array, &buf_without_null, &output_without_null,
                              &validate_non_nullable));

    Array<RectPtr> array3;
    Deserialize_(output_without_null, &array3);
    EXPECT_FALSE(array3[0].is_null());
    EXPECT_EQ(-1, array3[0]->x);
    EXPECT_EQ(-2, array3[0]->y);
    EXPECT_EQ(-3, array3[0]->width);
    EXPECT_EQ(-4, array3[0]->height);
    EXPECT_FALSE(array3[1].is_null());
    EXPECT_EQ(1, array3[1]->x);
    EXPECT_EQ(2, array3[1]->y);
    EXPECT_EQ(3, array3[1]->width);
    EXPECT_EQ(4, array3[1]->height);
  }
}

}  // namespace
}  // namespace test
}  // namespace fidl
