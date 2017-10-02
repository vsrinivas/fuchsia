// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/compiler/interfaces/tests/rect.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/map_serialization.h"
#include "lib/fidl/cpp/bindings/internal/validate_params.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/tests/util/container_test_util.h"
#include "lib/fxl/arraysize.h"

namespace fidl {
namespace test {

namespace {

using fidl::internal::Array_Data;
using fidl::internal::ArrayValidateParams;
using fidl::internal::FixedBufferForTesting;
using fidl::internal::Map_Data;
using fidl::internal::String_Data;
using fidl::internal::ValidationError;

struct StringIntData {
  const char* string_data;
  int int_data;
} kStringIntData[] = {
    {"one", 1},
    {"two", 2},
    {"three", 3},
    {"four", 4},
},
kStringIntDataSorted[] = {
    {"four", 4},
    {"one", 1},
    {"three", 3},
    {"two", 2},
};

const size_t kStringIntDataSize = 4;

TEST(MapTest, Testability) {
  Map<int32_t, int32_t> map;
  EXPECT_FALSE(map);
  EXPECT_TRUE(map.is_null());

  map[123] = 456;
  EXPECT_TRUE(map);
  EXPECT_FALSE(map.is_null());
}

// Tests that basic Map operations work.
TEST(MapTest, InsertWorks) {
  Map<String, int> map;
  for (size_t i = 0; i < kStringIntDataSize; ++i)
    map.insert(kStringIntData[i].string_data, kStringIntData[i].int_data);

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    EXPECT_EQ(kStringIntData[i].int_data,
              map.at(kStringIntData[i].string_data));
  }
}

TEST(MapTest, TestIndexOperator) {
  Map<String, int> map;
  for (size_t i = 0; i < kStringIntDataSize; ++i)
    map[kStringIntData[i].string_data] = kStringIntData[i].int_data;

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    EXPECT_EQ(kStringIntData[i].int_data,
              map.at(kStringIntData[i].string_data));
  }
}

// Tests that range-based for loops work, and that the ordering is correct
TEST(MapTest, RangeBasedForLoops) {
  Map<String, int> map;

  for (size_t i = 0; i < kStringIntDataSize; ++i)
    map.insert(kStringIntData[i].string_data, kStringIntData[i].int_data);

  size_t idx = 0;
  for (auto &it : map) {
    EXPECT_EQ(kStringIntDataSorted[idx].string_data, it.GetKey());
    EXPECT_EQ(kStringIntDataSorted[idx].int_data, it.GetValue());
    idx++;
  }
  EXPECT_EQ(idx, map.size());

  idx = 0;
  for (const auto &it : map) {
    EXPECT_EQ(kStringIntDataSorted[idx].string_data, it.GetKey());
    EXPECT_EQ(kStringIntDataSorted[idx].int_data, it.GetValue());
    idx++;
  }
  EXPECT_EQ(idx, map.size());
}


TEST(MapTest, TestIndexOperatorAsRValue) {
  Map<String, int> map;
  for (size_t i = 0; i < kStringIntDataSize; ++i)
    map.insert(kStringIntData[i].string_data, kStringIntData[i].int_data);

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    EXPECT_EQ(kStringIntData[i].int_data, map[kStringIntData[i].string_data]);
  }
}

TEST(MapTest, TestIndexOperatorMoveOnly) {
  ASSERT_EQ(0u, MoveOnlyType::num_instances());
  fidl::Map<fidl::String, fidl::Array<int32_t>> map;
  std::vector<MoveOnlyType*> value_ptrs;

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    const char* key = kStringIntData[i].string_data;
    auto array = Array<int32_t>::New(1);
    array[0] = kStringIntData[i].int_data;
    map[key] = std::move(array);
    EXPECT_TRUE(map);
  }

  // We now read back that data, to test the behavior of operator[].
  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    auto it = map.find(kStringIntData[i].string_data);
    ASSERT_TRUE(it != map.end());
    ASSERT_EQ(1u, it.GetValue().size());
    EXPECT_EQ(kStringIntData[i].int_data, it.GetValue()[0]);
  }
}

TEST(MapTest, ConstructedFromArray) {
  auto keys = Array<String>::New(kStringIntDataSize);
  auto values = Array<int>::New(kStringIntDataSize);
  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    keys[i] = kStringIntData[i].string_data;
    values[i] = kStringIntData[i].int_data;
  }

  Map<String, int> map(std::move(keys), std::move(values));

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    EXPECT_EQ(kStringIntData[i].int_data,
              map.at(fidl::String(kStringIntData[i].string_data)));
  }
}

TEST(MapTest, Insert_Copyable) {
  ASSERT_EQ(0u, CopyableType::num_instances());
  fidl::Map<fidl::String, CopyableType> map;
  std::vector<CopyableType*> value_ptrs;

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    const char* key = kStringIntData[i].string_data;
    CopyableType value;
    value_ptrs.push_back(value.ptr());
    map.insert(key, value);
    ASSERT_EQ(i + 1, map.size());
    ASSERT_EQ(i + 1, value_ptrs.size());
    EXPECT_EQ(map.size() + 1, CopyableType::num_instances());
    EXPECT_TRUE(map.at(key).copied());
    EXPECT_EQ(value_ptrs[i], map.at(key).ptr());
    map.at(key).ResetCopied();
    EXPECT_TRUE(map);
  }

  // std::map doesn't have a capacity() method like std::vector so this test is
  // a lot more boring.

  map.reset();
  EXPECT_EQ(0u, CopyableType::num_instances());
}

TEST(MapTest, Insert_MoveOnly) {
  ASSERT_EQ(0u, MoveOnlyType::num_instances());
  fidl::Map<fidl::String, MoveOnlyType> map;
  std::vector<MoveOnlyType*> value_ptrs;

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    const char* key = kStringIntData[i].string_data;
    MoveOnlyType value;
    value_ptrs.push_back(value.ptr());
    map.insert(key, std::move(value));
    ASSERT_EQ(i + 1, map.size());
    ASSERT_EQ(i + 1, value_ptrs.size());
    EXPECT_EQ(map.size() + 1, MoveOnlyType::num_instances());
    EXPECT_TRUE(map.at(key).moved());
    EXPECT_EQ(value_ptrs[i], map.at(key).ptr());
    map.at(key).ResetMoved();
    EXPECT_TRUE(map);
  }

  // std::map doesn't have a capacity() method like std::vector so this test is
  // a lot more boring.

  map.reset();
  EXPECT_EQ(0u, MoveOnlyType::num_instances());
}

TEST(MapTest, IndexOperator_MoveOnly) {
  ASSERT_EQ(0u, MoveOnlyType::num_instances());
  fidl::Map<fidl::String, MoveOnlyType> map;
  std::vector<MoveOnlyType*> value_ptrs;

  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    const char* key = kStringIntData[i].string_data;
    MoveOnlyType value;
    value_ptrs.push_back(value.ptr());
    map[key] = std::move(value);
    ASSERT_EQ(i + 1, map.size());
    ASSERT_EQ(i + 1, value_ptrs.size());
    EXPECT_EQ(map.size() + 1, MoveOnlyType::num_instances());
    EXPECT_TRUE(map.at(key).moved());
    EXPECT_EQ(value_ptrs[i], map.at(key).ptr());
    map.at(key).ResetMoved();
    EXPECT_TRUE(map);
  }

  // std::map doesn't have a capacity() method like std::vector so this test is
  // a lot more boring.

  map.reset();
  EXPECT_EQ(0u, MoveOnlyType::num_instances());
}

TEST(MapTest, STLToMojo) {
  std::map<std::string, int> stl_data;
  for (size_t i = 0; i < kStringIntDataSize; ++i)
    stl_data[kStringIntData[i].string_data] = kStringIntData[i].int_data;

  Map<String, int32_t> mojo_data = Map<String, int32_t>::From(stl_data);
  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    EXPECT_EQ(kStringIntData[i].int_data,
              mojo_data.at(kStringIntData[i].string_data));
  }
}

TEST(MapTest, MojoToSTL) {
  Map<String, int32_t> mojo_map;
  for (size_t i = 0; i < kStringIntDataSize; ++i)
    mojo_map.insert(kStringIntData[i].string_data, kStringIntData[i].int_data);

  std::map<std::string, int> stl_map =
      mojo_map.To<std::map<std::string, int>>();
  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    auto it = stl_map.find(kStringIntData[i].string_data);
    ASSERT_TRUE(it != stl_map.end());
    EXPECT_EQ(kStringIntData[i].int_data, it->second);
  }
}

TEST(MapTest, MapArrayClone) {
  Map<String, Array<String>> m;
  for (size_t i = 0; i < kStringIntDataSize; ++i) {
    Array<String> s;
    s.push_back(kStringIntData[i].string_data);
    m.insert(kStringIntData[i].string_data, std::move(s));
  }

  Map<String, Array<String>> m2 = m.Clone();

  for (auto it = m2.begin(); it != m2.end(); ++it) {
    ASSERT_EQ(1u, it.GetValue().size());
    EXPECT_EQ(it.GetKey(), it.GetValue().at(0));
  }
}

TEST(MapTest, ArrayOfMap) {
  {
    auto array = Array<Map<int32_t, int8_t>>::New(1);
    array[0].insert(1, 42);

    size_t size = GetSerializedSize_(array);
    FixedBufferForTesting buf(size);
    Array_Data<Map_Data<int32_t, int8_t>*>* data = nullptr;
    ArrayValidateParams validate_params(
        0, false, new ArrayValidateParams(0, false, nullptr));
    EXPECT_EQ(ValidationError::NONE,
              SerializeArray_(&array, &buf, &data, &validate_params));

    Array<Map<int32_t, int8_t>> deserialized_array;
    Deserialize_(data, &deserialized_array);

    ASSERT_EQ(1u, deserialized_array.size());
    ASSERT_EQ(1u, deserialized_array[0].size());
    ASSERT_EQ(42, deserialized_array[0].at(1));
  }

  {
    auto array = Array<Map<String, Array<bool>>>::New(1);
    auto map_value = Array<bool>::New(2);
    map_value[0] = false;
    map_value[1] = true;
    array[0].insert("hello world", std::move(map_value));

    size_t size = GetSerializedSize_(array);
    FixedBufferForTesting buf(size);
    Array_Data<Map_Data<String_Data*, Array_Data<bool>*>*>* data = nullptr;
    ArrayValidateParams validate_params(
        0, false, new ArrayValidateParams(
                      0, false, new ArrayValidateParams(0, false, nullptr)));
    EXPECT_EQ(ValidationError::NONE,
              SerializeArray_(&array, &buf, &data, &validate_params));

    Array<Map<String, Array<bool>>> deserialized_array;
    Deserialize_(data, &deserialized_array);

    ASSERT_EQ(1u, deserialized_array.size());
    ASSERT_EQ(1u, deserialized_array[0].size());
    ASSERT_FALSE(deserialized_array[0].at("hello world")[0]);
    ASSERT_TRUE(deserialized_array[0].at("hello world")[1]);
  }
}

TEST(MapTest, Serialization_MapWithScopedEnumKeys) {
  enum class TestEnum : int32_t {
    E0,
    E1,
    E2,
    E3,
  };
  static const TestEnum TEST_KEYS[] = {
      TestEnum::E0, TestEnum::E2, TestEnum::E1, TestEnum::E3,
  };
  static const uint32_t TEST_VALS[] = {17, 29, 5, 61};

  ASSERT_EQ(arraysize(TEST_KEYS), arraysize(TEST_VALS));

  Map<TestEnum, uint32_t> test_map;
  for (size_t i = 0; i < arraysize(TEST_KEYS); ++i) {
    test_map[TEST_KEYS[i]] = TEST_VALS[i];
  }

  size_t size = GetSerializedSize_(test_map);
  FixedBufferForTesting buf(size);
  Map_Data<int32_t, uint32_t>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);

  SerializeMap_(&test_map, &buf, &data, &validate_params);

  Map<TestEnum, uint32_t> test_map2;
  Deserialize_(data, &test_map2);

  EXPECT_TRUE(test_map2.Equals(test_map));

  for (auto iter = test_map.cbegin(); iter != test_map.cend(); ++iter) {
    ASSERT_NE(test_map2.find(iter.GetKey()), test_map2.end());
    EXPECT_EQ(test_map.at(iter.GetKey()), test_map.at(iter.GetKey()));
  }

  for (auto iter = test_map2.cbegin(); iter != test_map2.cend(); ++iter) {
    ASSERT_NE(test_map.find(iter.GetKey()), test_map.end());
    EXPECT_EQ(test_map2.at(iter.GetKey()), test_map2.at(iter.GetKey()));
  }
}

TEST(MapTest, Serialization_MapWithScopedEnumVals) {
  enum class TestEnum : int32_t {
    E0,
    E1,
    E2,
    E3,
  };
  static const uint32_t TEST_KEYS[] = {17, 29, 5, 61};
  static const TestEnum TEST_VALS[] = {
      TestEnum::E0, TestEnum::E2, TestEnum::E1, TestEnum::E3,
  };

  ASSERT_EQ(arraysize(TEST_KEYS), arraysize(TEST_VALS));

  Map<uint32_t, TestEnum> test_map;
  for (size_t i = 0; i < arraysize(TEST_KEYS); ++i) {
    test_map[TEST_KEYS[i]] = TEST_VALS[i];
  }

  size_t size = GetSerializedSize_(test_map);
  FixedBufferForTesting buf(size);
  Map_Data<uint32_t, int32_t>* data = nullptr;
  ArrayValidateParams validate_params(0, false, nullptr);

  SerializeMap_(&test_map, &buf, &data, &validate_params);

  Map<uint32_t, TestEnum> test_map2;
  Deserialize_(data, &test_map2);

  EXPECT_TRUE(test_map2.Equals(test_map));

  for (auto iter = test_map.cbegin(); iter != test_map.cend(); ++iter) {
    ASSERT_NE(test_map2.find(iter.GetKey()), test_map2.end());
    EXPECT_EQ(test_map.at(iter.GetKey()), test_map.at(iter.GetKey()));
  }

  for (auto iter = test_map2.cbegin(); iter != test_map2.cend(); ++iter) {
    ASSERT_NE(test_map.find(iter.GetKey()), test_map.end());
    EXPECT_EQ(test_map2.at(iter.GetKey()), test_map2.at(iter.GetKey()));
  }
}

// Test serialization/deserialization of a map with null elements.
TEST(MapTest, Serialization_MapOfNullableStructs) {
  ArrayValidateParams validate_nullable(2, true, nullptr);
  ArrayValidateParams validate_non_nullable(2, false, nullptr);

  Map<uint32_t, RectPtr> map;
  map[0] = RectPtr();
  map[1] = Rect::New();
  map[1]->x = 1;
  map[1]->y = 2;
  map[1]->width = 3;
  map[1]->height = 4;
  EXPECT_TRUE(map[0].is_null());
  EXPECT_TRUE(!map[1].is_null());

  size_t size = GetSerializedSize_(map);
  EXPECT_EQ(8u +                       // map header
                (8u + 8u) +            // pointers to keys and values array
                (8u + 2 * 4u) +        // keys array data
                (8u +                  // values array data
                 (8u) +                // 1 null value
                 (8u + 8U + 4 * 4U)),  // 1 Rect value
            size);

#ifdef NDEBUG // In debug builds serialization failures abort
  // 1. Should not be able to serialize null elements.
  {
    FixedBufferForTesting buf(size);
    Map_Data<int32_t, Rect::Data_*>* data = nullptr;
    EXPECT_EQ(ValidationError::UNEXPECTED_NULL_POINTER,
              SerializeMap_(&map, &buf, &data, &validate_non_nullable));
  }
#endif

  // 2. Successfully serialize null elements.
  FixedBufferForTesting buf(size);
  Map_Data<int32_t, Rect::Data_*>* data = nullptr;
  EXPECT_EQ(ValidationError::NONE,
            SerializeMap_(&map, &buf, &data, &validate_nullable));
  EXPECT_NE(nullptr, data);

  // 3. Deserialize deserialize null elements.
  Map<uint32_t, RectPtr> map2;
  EXPECT_EQ(0u, map2.size());
  EXPECT_TRUE(map2.is_null());
  Deserialize_(data, &map2);
  EXPECT_EQ(2u, map2.size());
  EXPECT_FALSE(map2.is_null());
  EXPECT_TRUE(map2[0].is_null());
  EXPECT_FALSE(map2[1].is_null());
  EXPECT_EQ(1, map2[1]->x);
  EXPECT_EQ(2, map2[1]->y);
  EXPECT_EQ(3, map2[1]->width);
  EXPECT_EQ(4, map2[1]->height);
}

}  // namespace
}  // namespace test
}  // namespace fidl
