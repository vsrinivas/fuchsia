// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/map_serialization.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/tests/util/test_utils.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/compiler/interfaces/tests/test_structs.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/test_unions.fidl.h"

namespace fidl {
namespace test {

TEST(UnionTest, PlainOldDataGetterSetter) {
  PodUnionPtr pod(PodUnion::New());

  pod->set_f_int8(10);
  EXPECT_EQ(10, pod->get_f_int8());
  EXPECT_TRUE(pod->is_f_int8());
  EXPECT_FALSE(pod->is_f_int8_other());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_INT8);

  pod->set_f_uint8(11);
  EXPECT_EQ(11, pod->get_f_uint8());
  EXPECT_TRUE(pod->is_f_uint8());
  EXPECT_FALSE(pod->is_f_int8());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_UINT8);

  pod->set_f_int16(12);
  EXPECT_EQ(12, pod->get_f_int16());
  EXPECT_TRUE(pod->is_f_int16());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_INT16);

  pod->set_f_uint16(13);
  EXPECT_EQ(13, pod->get_f_uint16());
  EXPECT_TRUE(pod->is_f_uint16());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_UINT16);

  pod->set_f_int32(14);
  EXPECT_EQ(14, pod->get_f_int32());
  EXPECT_TRUE(pod->is_f_int32());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_INT32);

  pod->set_f_uint32(static_cast<uint32_t>(15));
  EXPECT_EQ(static_cast<uint32_t>(15), pod->get_f_uint32());
  EXPECT_TRUE(pod->is_f_uint32());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_UINT32);

  pod->set_f_int64(16);
  EXPECT_EQ(16, pod->get_f_int64());
  EXPECT_TRUE(pod->is_f_int64());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_INT64);

  pod->set_f_uint64(static_cast<uint64_t>(17));
  EXPECT_EQ(static_cast<uint64_t>(17), pod->get_f_uint64());
  EXPECT_TRUE(pod->is_f_uint64());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_UINT64);

  pod->set_f_float(1.5);
  EXPECT_EQ(1.5, pod->get_f_float());
  EXPECT_TRUE(pod->is_f_float());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_FLOAT);

  pod->set_f_double(1.9);
  EXPECT_EQ(1.9, pod->get_f_double());
  EXPECT_TRUE(pod->is_f_double());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_DOUBLE);

  pod->set_f_bool(true);
  EXPECT_TRUE(pod->get_f_bool());
  pod->set_f_bool(false);
  EXPECT_FALSE(pod->get_f_bool());
  EXPECT_TRUE(pod->is_f_bool());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_BOOL);

  pod->set_f_enum(AnEnum::SECOND);
  EXPECT_EQ(AnEnum::SECOND, pod->get_f_enum());
  EXPECT_TRUE(pod->is_f_enum());
  EXPECT_EQ(pod->which(), PodUnion::Tag::F_ENUM);
}

TEST(UnionTest, PodEquals) {
  PodUnionPtr pod1(PodUnion::New());
  PodUnionPtr pod2(PodUnion::New());

  pod1->set_f_int8(10);
  pod2->set_f_int8(10);
  EXPECT_TRUE(pod1.Equals(pod2));

  pod2->set_f_int8(11);
  EXPECT_FALSE(pod1.Equals(pod2));

  pod2->set_f_int8_other(10);
  EXPECT_FALSE(pod1.Equals(pod2));
}

TEST(UnionTest, PodClone) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(10);

  PodUnionPtr pod_clone = pod.Clone();
  EXPECT_EQ(10, pod_clone->get_f_int8());
  EXPECT_TRUE(pod_clone->is_f_int8());
  EXPECT_EQ(pod_clone->which(), PodUnion::Tag::F_INT8);
}

TEST(UnionTest, PodSerialization) {
  PodUnionPtr pod1(PodUnion::New());
  pod1->set_f_int8(10);

  size_t size = GetSerializedSize_(pod1);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::PodUnion_Data::New(&buf);
  SerializeUnion_(pod1.get(), &buf, &data);

  PodUnionPtr pod2(PodUnion::New());
  Deserialize_(data, pod2.get());

  EXPECT_EQ(10, pod2->get_f_int8());
  EXPECT_TRUE(pod2->is_f_int8());
  EXPECT_EQ(pod2->which(), PodUnion::Tag::F_INT8);
}

TEST(UnionTest, EnumSerialization) {
  PodUnionPtr pod1(PodUnion::New());
  pod1->set_f_enum(AnEnum::SECOND);

  size_t size = GetSerializedSize_(pod1);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::PodUnion_Data::New(&buf);
  SerializeUnion_(pod1.get(), &buf, &data);

  PodUnionPtr pod2 = PodUnion::New();
  Deserialize_(data, pod2.get());

  EXPECT_EQ(AnEnum::SECOND, pod2->get_f_enum());
  EXPECT_TRUE(pod2->is_f_enum());
  EXPECT_EQ(pod2->which(), PodUnion::Tag::F_ENUM);
}

TEST(UnionTest, PodValidation) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(10);

  size_t size = GetSerializedSize_(pod);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::PodUnion_Data::New(&buf);
  SerializeUnion_(pod.get(), &buf, &data);
  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::PodUnion_Data::Validate(raw_buf, &bounds_checker, false,
                                              nullptr));
  free(raw_buf);
}

TEST(UnionTest, SerializeNotNull) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(0);
  size_t size = GetSerializedSize_(pod);
  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::PodUnion_Data::New(&buf);
  SerializeUnion_(pod.get(), &buf, &data);
  EXPECT_FALSE(data->is_null());
}

TEST(UnionTest, SerializeIsNull) {
  PodUnionPtr pod;
  size_t size = GetSerializedSize_(pod);
  EXPECT_EQ(16U, size);
  fidl::internal::FixedBufferForTesting buf(size);
  internal::PodUnion_Data* data = internal::PodUnion_Data::New(&buf);

  // Check that dirty output buffers are handled correctly by serialization.
  data->size = 16U;
  data->tag = PodUnion::Tag::F_UINT16;
  data->data.f_f_int16 = 20;

  SerializeUnion_(pod.get(), &buf, &data);
  EXPECT_TRUE(data->is_null());

  PodUnionPtr pod2 = PodUnion::New();
  Deserialize_(data, pod2.get());
  EXPECT_EQ(pod2->which(), PodUnion::Tag::__UNKNOWN__);

  {
    PodUnionPtr pod;
    size_t size = GetSerializedSize_(pod);
    EXPECT_EQ(16U, size);
    fidl::internal::FixedBufferForTesting buf(size);
    auto* data = internal::PodUnion_Data::New(&buf);
    SerializeUnion_(pod.get(), &buf, &data);
    EXPECT_EQ(static_cast<internal::PodUnion_Data::PodUnion_Tag>(0), data->tag);
    EXPECT_EQ(0u, data->size);
    EXPECT_EQ(0u, data->data.unknown);
  }
}

TEST(UnionTest, NullValidation) {
  void* buf = nullptr;
  fidl::internal::BoundsChecker bounds_checker(buf, 0, 0);
  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      internal::PodUnion_Data::Validate(buf, &bounds_checker, false, nullptr));
}

TEST(UnionTest, OutOfAlignmentValidation) {
  size_t size = sizeof(internal::PodUnion_Data);
  // Get an aligned object and shift the alignment.
  fidl::internal::FixedBufferForTesting aligned_buf(size + 1);
  void* raw_buf = aligned_buf.Leak();
  char* buf = reinterpret_cast<char*>(raw_buf) + 1;

  internal::PodUnion_Data* data =
      reinterpret_cast<internal::PodUnion_Data*>(buf);
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_NE(
      fidl::internal::ValidationError::NONE,
      internal::PodUnion_Data::Validate(buf, &bounds_checker, false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, OOBValidation) {
  size_t size = sizeof(internal::PodUnion_Data) - 1;
  fidl::internal::FixedBufferForTesting buf(size);
  internal::PodUnion_Data* data = internal::PodUnion_Data::New(&buf);
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  void* raw_buf = buf.Leak();
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::PodUnion_Data::Validate(raw_buf, &bounds_checker, false,
                                              nullptr));
  free(raw_buf);
}

TEST(UnionTest, UnknownTagDeserialization) {
  size_t size = sizeof(internal::PodUnion_Data);
  fidl::internal::FixedBufferForTesting buf(size);
  internal::PodUnion_Data* data = internal::PodUnion_Data::New(&buf);
  data->size = size;
  data->tag = static_cast<internal::PodUnion_Data::PodUnion_Tag>(100);

  PodUnionPtr pod2 = PodUnion::New();
  Deserialize_(data, pod2.get());

  EXPECT_TRUE(pod2->has_unknown_tag());
}

TEST(UnionTest, UnknownTagValidation) {
  size_t size = sizeof(internal::PodUnion_Data);
  fidl::internal::FixedBufferForTesting buf(size);
  internal::PodUnion_Data* data = internal::PodUnion_Data::New(&buf);
  data->size = size;
  data->tag = static_cast<internal::PodUnion_Data::PodUnion_Tag>(100);
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  void* raw_buf = buf.Leak();
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::PodUnion_Data::Validate(raw_buf, &bounds_checker, false,
                                              nullptr));
  free(raw_buf);
}

TEST(UnionTest, StringGetterSetter) {
  ObjectUnionPtr pod(ObjectUnion::New());

  String hello("hello world");
  pod->set_f_string(hello);
  EXPECT_EQ(hello, pod->get_f_string());
  EXPECT_TRUE(pod->is_f_string());
  EXPECT_EQ(pod->which(), ObjectUnion::Tag::F_STRING);
}

TEST(UnionTest, StringEquals) {
  ObjectUnionPtr pod1(ObjectUnion::New());
  ObjectUnionPtr pod2(ObjectUnion::New());

  pod1->set_f_string("hello world");
  pod2->set_f_string("hello world");
  EXPECT_TRUE(pod1.Equals(pod2));

  pod2->set_f_string("hello universe");
  EXPECT_FALSE(pod1.Equals(pod2));
}

TEST(UnionTest, StringClone) {
  ObjectUnionPtr pod(ObjectUnion::New());

  String hello("hello world");
  pod->set_f_string(hello);
  ObjectUnionPtr pod_clone = pod.Clone();
  EXPECT_EQ(hello, pod_clone->get_f_string());
  EXPECT_TRUE(pod_clone->is_f_string());
  EXPECT_EQ(pod_clone->which(), ObjectUnion::Tag::F_STRING);
}

TEST(UnionTest, StringSerialization) {
  ObjectUnionPtr pod1(ObjectUnion::New());

  String hello("hello world");
  pod1->set_f_string(hello);

  size_t size = GetSerializedSize_(pod1);
  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(pod1.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  ObjectUnionPtr pod2 = ObjectUnion::New();
  Deserialize_(data, pod2.get());
  EXPECT_EQ(hello, pod2->get_f_string());
  EXPECT_TRUE(pod2->is_f_string());
  EXPECT_EQ(pod2->which(), ObjectUnion::Tag::F_STRING);
}

TEST(UnionTest, NullStringValidation) {
  size_t size = sizeof(internal::ObjectUnion_Data);
  fidl::internal::FixedBufferForTesting buf(size);
  internal::ObjectUnion_Data* data = internal::ObjectUnion_Data::New(&buf);
  data->size = 16;
  data->tag = internal::ObjectUnion_Data::ObjectUnion_Tag::F_STRING;
  data->data.unknown = 0x0;
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  void* raw_buf = buf.Leak();
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, StringPointerOverflowValidation) {
  size_t size = sizeof(internal::ObjectUnion_Data);
  fidl::internal::FixedBufferForTesting buf(size);
  internal::ObjectUnion_Data* data = internal::ObjectUnion_Data::New(&buf);
  data->size = 16;
  data->tag = internal::ObjectUnion_Data::ObjectUnion_Tag::F_STRING;
  data->data.unknown = 0xFFFFFFFFFFFFFFFF;
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  void* raw_buf = buf.Leak();
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, StringValidateOOB) {
  size_t size = 32;
  fidl::internal::FixedBufferForTesting buf(size);
  internal::ObjectUnion_Data* data = internal::ObjectUnion_Data::New(&buf);
  data->size = 16;
  data->tag = internal::ObjectUnion_Data::ObjectUnion_Tag::F_STRING;

  data->data.f_f_string.offset = 8;
  char* ptr = reinterpret_cast<char*>(&data->data.f_f_string);
  fidl::internal::ArrayHeader* array_header =
      reinterpret_cast<fidl::internal::ArrayHeader*>(ptr + *ptr);
  array_header->num_bytes = 20;  // This should go out of bounds.
  array_header->num_elements = 20;
  fidl::internal::BoundsChecker bounds_checker(data, 32, 0);
  void* raw_buf = buf.Leak();
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

// TODO(azani): Move back in array_unittest.cc when possible.
// Array tests
TEST(UnionTest, PodUnionInArray) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union_array = Array<PodUnionPtr>::New(2);
  small_struct->pod_union_array[0] = PodUnion::New();
  small_struct->pod_union_array[1] = PodUnion::New();

  small_struct->pod_union_array[0]->set_f_int8(10);
  small_struct->pod_union_array[1]->set_f_int16(12);

  EXPECT_EQ(10, small_struct->pod_union_array[0]->get_f_int8());
  EXPECT_EQ(12, small_struct->pod_union_array[1]->get_f_int16());
}

TEST(UnionTest, PodUnionInArraySerialization) {
  auto array = Array<PodUnionPtr>::New(2);
  array[0] = PodUnion::New();
  array[1] = PodUnion::New();

  array[0]->set_f_int8(10);
  array[1]->set_f_int16(12);
  EXPECT_EQ(2U, array.size());

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(40U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  fidl::internal::Array_Data<internal::PodUnion_Data>* data = nullptr;
  fidl::internal::ArrayValidateParams validate_params(0, false, nullptr);
  SerializeArray_(&array, &buf, &data, &validate_params);

  Array<PodUnionPtr> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(2U, array2.size());

  EXPECT_EQ(10, array2[0]->get_f_int8());
  EXPECT_EQ(12, array2[1]->get_f_int16());
}

TEST(UnionTest, PodUnionInArrayValidation) {
  auto array = Array<PodUnionPtr>::New(2);
  array[0] = PodUnion::New();
  array[1] = PodUnion::New();

  array[0]->set_f_int8(10);
  array[1]->set_f_int16(12);

  size_t size = GetSerializedSize_(array);

  fidl::internal::FixedBufferForTesting buf(size);
  fidl::internal::Array_Data<internal::PodUnion_Data>* data = nullptr;
  fidl::internal::ArrayValidateParams validate_params(0, false, nullptr);
  SerializeArray_(&array, &buf, &data, &validate_params);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 1);

  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Array<PodUnionPtr>::Data_::Validate(raw_buf, &bounds_checker,
                                                &validate_params, nullptr));
  free(raw_buf);
}
TEST(UnionTest, PodUnionInArraySerializationWithNull) {
  auto array = Array<PodUnionPtr>::New(2);
  array[0] = PodUnion::New();

  array[0]->set_f_int8(10);
  EXPECT_EQ(2U, array.size());

  size_t size = GetSerializedSize_(array);
  EXPECT_EQ(40U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  fidl::internal::Array_Data<internal::PodUnion_Data>* data = nullptr;
  fidl::internal::ArrayValidateParams validate_params(0, true, nullptr);
  SerializeArray_(&array, &buf, &data, &validate_params);

  Array<PodUnionPtr> array2;
  Deserialize_(data, &array2);

  EXPECT_EQ(2U, array2.size());

  EXPECT_EQ(10, array2[0]->get_f_int8());
  EXPECT_TRUE(array2[1].is_null());
}

// TODO(azani): Move back in struct_unittest.cc when possible.
// Struct tests
TEST(UnionTest, Clone_Union) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union = PodUnion::New();
  small_struct->pod_union->set_f_int8(10);

  SmallStructPtr clone = small_struct.Clone();
  EXPECT_EQ(10, clone->pod_union->get_f_int8());
}

// Serialization test of a struct with a union of plain old data.
TEST(UnionTest, Serialization_UnionOfPods) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union = PodUnion::New();
  small_struct->pod_union->set_f_int32(10);

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallStruct_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(small_struct.get(), &buf, &data));

  SmallStructPtr deserialized(SmallStruct::New());
  Deserialize_(data, deserialized.get());

  EXPECT_EQ(10, deserialized->pod_union->get_f_int32());
}

// Serialization test of a struct with a union of structs.
TEST(UnionTest, Serialization_UnionOfObjects) {
  SmallObjStructPtr obj_struct(SmallObjStruct::New());
  obj_struct->obj_union = ObjectUnion::New();
  String hello("hello world");
  obj_struct->obj_union->set_f_string(hello);

  size_t size = GetSerializedSize_(*obj_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallObjStruct_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(obj_struct.get(), &buf, &data));

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  SmallObjStructPtr deserialized(SmallObjStruct::New());
  Deserialize_(data, deserialized.get());

  EXPECT_EQ(hello, deserialized->obj_union->get_f_string());
}

// Validation test of a struct with a union.
TEST(UnionTest, Validation_UnionsInStruct) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union = PodUnion::New();
  small_struct->pod_union->set_f_int32(10);

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallStruct_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(small_struct.get(), &buf, &data));

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      internal::SmallStruct_Data::Validate(raw_buf, &bounds_checker, nullptr));
  free(raw_buf);
}

// Validation test of a struct union fails due to unknown union tag.
TEST(UnionTest, Validation_PodUnionInStruct_Failure) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union = PodUnion::New();
  small_struct->pod_union->set_f_int32(10);

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallStruct_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(small_struct.get(), &buf, &data));
  data->pod_union.tag = static_cast<internal::PodUnion_Data::PodUnion_Tag>(100);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      internal::SmallStruct_Data::Validate(raw_buf, &bounds_checker, nullptr));
  free(raw_buf);
}

// Validation fails due to non-nullable null union in struct.
TEST(UnionTest, Validation_NullUnion_Failure) {
  SmallStructNonNullableUnionPtr small_struct(
      SmallStructNonNullableUnion::New());

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallStructNonNullableUnion_Data* data =
      internal::SmallStructNonNullableUnion_Data::New(&buf);

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::SmallStructNonNullableUnion_Data::Validate(
                raw_buf, &bounds_checker, nullptr));
  free(raw_buf);
}

// Validation passes with nullable null union.
TEST(UnionTest, Validation_NullableUnion) {
  SmallStructPtr small_struct(SmallStruct::New());

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::SmallStruct_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(small_struct.get(), &buf, &data));

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(
      fidl::internal::ValidationError::NONE,
      internal::SmallStruct_Data::Validate(raw_buf, &bounds_checker, nullptr));
  free(raw_buf);
}

// Serialize a null union and deserialize it back to check that we have a null
// union.
TEST(UnionTest, Deserialize_NullableUnion) {
  char buf[1024];
  SmallStructPtr small_struct = SmallStruct::New();
  small_struct->Serialize(buf, sizeof(buf));
  EXPECT_TRUE(small_struct->pod_union.is_null());

  SmallStructPtr deserialized_struct = SmallStruct::New();
  EXPECT_TRUE(deserialized_struct->Deserialize(buf, sizeof(buf)));
  EXPECT_TRUE(deserialized_struct->pod_union.is_null());
}

// Validation passes with nullable null union containing non-nullable objects.
TEST(UnionTest, Validation_NullableObjectUnion) {
  StructNullObjectUnionPtr small_struct(StructNullObjectUnion::New());

  size_t size = GetSerializedSize_(*small_struct);

  fidl::internal::FixedBufferForTesting buf(size);
  internal::StructNullObjectUnion_Data* data = nullptr;
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            Serialize_(small_struct.get(), &buf, &data));

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::StructNullObjectUnion_Data::Validate(
                raw_buf, &bounds_checker, nullptr));
  free(raw_buf);
}

// TODO(azani): Move back in map_unittest.cc when possible.
// Map Tests
TEST(UnionTest, PodUnionInMap) {
  SmallStructPtr small_struct(SmallStruct::New());
  small_struct->pod_union_map = Map<String, PodUnionPtr>();
  small_struct->pod_union_map.insert("one", PodUnion::New());
  small_struct->pod_union_map.insert("two", PodUnion::New());

  small_struct->pod_union_map["one"]->set_f_int8(8);
  small_struct->pod_union_map["two"]->set_f_int16(16);

  EXPECT_EQ(8, small_struct->pod_union_map["one"]->get_f_int8());
  EXPECT_EQ(16, small_struct->pod_union_map["two"]->get_f_int16());
}

TEST(UnionTest, PodUnionInMapSerialization) {
  Map<String, PodUnionPtr> map;
  map.insert("one", PodUnion::New());
  map.insert("two", PodUnion::New());

  map["one"]->set_f_int8(8);
  map["two"]->set_f_int16(16);

  size_t size = GetSerializedSize_(map);
  EXPECT_EQ(120U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  fidl::internal::Map_Data<fidl::internal::String_Data*,
                           internal::PodUnion_Data>* data = nullptr;
  fidl::internal::ArrayValidateParams validate_params(0, false, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeMap_(&map, &buf, &data, &validate_params));

  Map<String, PodUnionPtr> map2;
  Deserialize_(data, &map2);

  EXPECT_EQ(8, map2["one"]->get_f_int8());
  EXPECT_EQ(16, map2["two"]->get_f_int16());
}

TEST(UnionTest, PodUnionInMapSerializationWithNull) {
  Map<String, PodUnionPtr> map;
  map.insert("one", PodUnion::New());
  map.insert("two", nullptr);

  map["one"]->set_f_int8(8);

  size_t size = GetSerializedSize_(map);
  EXPECT_EQ(120U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  fidl::internal::Map_Data<fidl::internal::String_Data*,
                           internal::PodUnion_Data>* data = nullptr;
  fidl::internal::ArrayValidateParams validate_params(0, true, nullptr);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            SerializeMap_(&map, &buf, &data, &validate_params));

  Map<String, PodUnionPtr> map2;
  Deserialize_(data, &map2);

  EXPECT_EQ(8, map2["one"]->get_f_int8());
  EXPECT_TRUE(map2["two"].is_null());
}

TEST(UnionTest, StructInUnionGetterSetterPasser) {
  DummyStructPtr dummy(DummyStruct::New());
  dummy->f_int8 = 8;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_dummy(std::move(dummy));

  EXPECT_EQ(8, obj->get_f_dummy()->f_int8);
}

TEST(UnionTest, StructInUnionSerialization) {
  DummyStructPtr dummy(DummyStruct::New());
  dummy->f_int8 = 8;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_dummy(std::move(dummy));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(32U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  ObjectUnionPtr obj2 = ObjectUnion::New();
  Deserialize_(data, obj2.get());
  EXPECT_EQ(8, obj2->get_f_dummy()->f_int8);
}

TEST(UnionTest, StructInUnionValidation) {
  DummyStructPtr dummy(DummyStruct::New());
  dummy->f_int8 = 8;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_dummy(std::move(dummy));

  size_t size = GetSerializedSize_(obj);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, StructInUnionValidationNonNullable) {
  DummyStructPtr dummy(nullptr);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_dummy(std::move(dummy));

  size_t size = GetSerializedSize_(obj);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, StructInUnionValidationNullable) {
  DummyStructPtr dummy(nullptr);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_nullable(std::move(dummy));

  size_t size = GetSerializedSize_(obj);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, ArrayInUnionGetterSetter) {
  auto array = Array<int8_t>::New(2);
  array[0] = 8;
  array[1] = 9;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_array_int8(std::move(array));

  EXPECT_EQ(8, obj->get_f_array_int8()[0]);
  EXPECT_EQ(9, obj->get_f_array_int8()[1]);
}

TEST(UnionTest, ArrayInUnionSerialization) {
  auto array = Array<int8_t>::New(2);
  array[0] = 8;
  array[1] = 9;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_array_int8(std::move(array));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(32U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  ObjectUnionPtr obj2 = ObjectUnion::New();
  Deserialize_(data, obj2.get());

  EXPECT_EQ(8, obj2->get_f_array_int8()[0]);
  EXPECT_EQ(9, obj2->get_f_array_int8()[1]);
}

TEST(UnionTest, ArrayInUnionValidation) {
  auto array = Array<int8_t>::New(2);
  array[0] = 8;
  array[1] = 9;

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_array_int8(std::move(array));

  size_t size = GetSerializedSize_(obj);
  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);

  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, MapInUnionGetterSetter) {
  Map<String, int8_t> map;
  map.insert("one", 1);
  map.insert("two", 2);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_map_int8(std::move(map));

  EXPECT_EQ(1, obj->get_f_map_int8()["one"]);
  EXPECT_EQ(2, obj->get_f_map_int8()["two"]);
}

TEST(UnionTest, MapInUnionSerialization) {
  Map<String, int8_t> map;
  map.insert("one", 1);
  map.insert("two", 2);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_map_int8(std::move(map));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(112U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  ObjectUnionPtr obj2 = ObjectUnion::New();
  Deserialize_(data, obj2.get());

  EXPECT_EQ(1, obj2->get_f_map_int8()["one"]);
  EXPECT_EQ(2, obj2->get_f_map_int8()["two"]);
}

TEST(UnionTest, MapInUnionValidation) {
  Map<String, int8_t> map;
  map.insert("one", 1);
  map.insert("two", 2);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_map_int8(std::move(map));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(112U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_TRUE(handles.empty());

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);

  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, UnionInUnionGetterSetter) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(10);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_pod_union(std::move(pod));

  EXPECT_EQ(10, obj->get_f_pod_union()->get_f_int8());
}

TEST(UnionTest, UnionInUnionSerialization) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(10);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_pod_union(std::move(pod));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(32U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  data->DecodePointersAndHandles(&handles);

  ObjectUnionPtr obj2 = ObjectUnion::New();
  Deserialize_(data, obj2.get());
  EXPECT_EQ(10, obj2->get_f_pod_union()->get_f_int8());
}

TEST(UnionTest, UnionInUnionValidation) {
  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int8(10);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_pod_union(std::move(pod));

  size_t size = GetSerializedSize_(obj);
  EXPECT_EQ(32U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, UnionInUnionValidationNonNullable) {
  PodUnionPtr pod(nullptr);

  ObjectUnionPtr obj(ObjectUnion::New());
  obj->set_f_pod_union(std::move(pod));

  size_t size = GetSerializedSize_(obj);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::ObjectUnion_Data::New(&buf);
  SerializeUnion_(obj.get(), &buf, &data);
  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 0);
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::ObjectUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, HandleInUnionGetterSetter) {
  zx::channel pipe0;
  zx::channel pipe1;

  zx::channel::create(0, &pipe0, &pipe1);

  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_message_pipe(std::move(pipe1));

  std::string golden("hello world");
  WriteTextMessage(pipe0, golden);

  std::string actual;
  ReadTextMessage(handle->get_f_message_pipe(), &actual);

  EXPECT_EQ(golden, actual);
}

TEST(UnionTest, HandleInUnionSerialization) {
  zx::channel pipe0;
  zx::channel pipe1;

  zx::channel::create(0, &pipe0, &pipe1);

  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_message_pipe(std::move(pipe1));

  size_t size = GetSerializedSize_(handle);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::HandleUnion_Data::New(&buf);
  SerializeUnion_(handle.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_EQ(1U, handles.size());
  data->DecodePointersAndHandles(&handles);

  HandleUnionPtr handle2(HandleUnion::New());
  Deserialize_(data, handle2.get());

  std::string golden("hello world");
  WriteTextMessage(pipe0, golden);

  std::string actual;
  ReadTextMessage(handle2->get_f_message_pipe(), &actual);

  EXPECT_EQ(golden, actual);
}

TEST(UnionTest, HandleInUnionValidation) {
  zx::channel pipe0;
  zx::channel pipe1;

  zx::channel::create(0, &pipe0, &pipe1);

  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_message_pipe(std::move(pipe1));

  size_t size = GetSerializedSize_(handle);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::HandleUnion_Data::New(&buf);
  SerializeUnion_(handle.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 1);
  EXPECT_EQ(fidl::internal::ValidationError::NONE,
            internal::HandleUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

TEST(UnionTest, HandleInUnionValidationNull) {
  zx::channel pipe;
  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_message_pipe(std::move(pipe));

  size_t size = GetSerializedSize_(handle);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::HandleUnion_Data::New(&buf);
  SerializeUnion_(handle.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);

  void* raw_buf = buf.Leak();
  fidl::internal::BoundsChecker bounds_checker(data,
                                               static_cast<uint32_t>(size), 1);
  EXPECT_NE(fidl::internal::ValidationError::NONE,
            internal::HandleUnion_Data::Validate(raw_buf, &bounds_checker,
                                                 false, nullptr));
  free(raw_buf);
}

class SmallCacheImpl : public SmallCache {
 public:
  SmallCacheImpl() : int_value_(0) {}
  ~SmallCacheImpl() override {}
  int64_t int_value() const { return int_value_; }

 private:
  void SetIntValue(int64_t int_value) override { int_value_ = int_value; }
  void GetIntValue(const GetIntValueCallback& callback) override {
    callback(int_value_);
  }

  int64_t int_value_;
};

TEST(UnionTest, InterfaceInUnion) {
  ClearAsyncWaiter();
  SmallCacheImpl impl;
  Binding<SmallCache> bindings(&impl);

  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_small_cache(bindings.NewBinding());

  auto small_cache =
      SmallCachePtr::Create(std::move(handle->get_f_small_cache()));
  small_cache->SetIntValue(10);
  WaitForAsyncWaiter();
  EXPECT_EQ(10, impl.int_value());
}

TEST(UnionTest, InterfaceInUnionSerialization) {
  SmallCacheImpl impl;
  Binding<SmallCache> bindings(&impl);

  HandleUnionPtr handle(HandleUnion::New());
  handle->set_f_small_cache(bindings.NewBinding());
  size_t size = GetSerializedSize_(handle);
  EXPECT_EQ(16U, size);

  fidl::internal::FixedBufferForTesting buf(size);
  auto* data = internal::HandleUnion_Data::New(&buf);
  SerializeUnion_(handle.get(), &buf, &data);

  std::vector<zx_handle_t> handles;
  data->EncodePointersAndHandles(&handles);
  EXPECT_EQ(1U, handles.size());
  data->DecodePointersAndHandles(&handles);

  HandleUnionPtr handle2(HandleUnion::New());
  Deserialize_(data, handle2.get());

  auto small_cache =
      SmallCachePtr::Create(std::move(handle2->get_f_small_cache()));
  small_cache->SetIntValue(10);
  WaitForAsyncWaiter();
  EXPECT_EQ(10, impl.int_value());
}

class UnionInterfaceImpl : public UnionInterface {
 public:
  UnionInterfaceImpl() {}
  ~UnionInterfaceImpl() override {}

 private:
  void Echo(PodUnionPtr in, const EchoCallback& callback) override {
    callback(std::move(in));
  }
};

TEST(UnionTest, UnionInInterface) {
  ClearAsyncWaiter();
  UnionInterfaceImpl impl;
  UnionInterfacePtr ptr;
  Binding<UnionInterface> bindings(&impl, ptr.NewRequest());

  PodUnionPtr pod(PodUnion::New());
  pod->set_f_int16(16);

  ptr->Echo(std::move(pod),
            [](PodUnionPtr out) { EXPECT_EQ(16, out->get_f_int16()); });
  WaitForAsyncWaiter();
}

}  // namespace test
}  // namespace fidl
