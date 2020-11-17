// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"

namespace fidl {
namespace test {
namespace misc {
namespace {

// Takes a collection of distinct elements, and checks that the comparison
// operators are correct.
template <typename A>
bool CheckComparison(const std::vector<A>& v) {
  for (size_t i = 0; i < v.size(); ++i) {
    EXPECT_TRUE(fidl::Equals(v[i], fidl::Clone(v[i])));
    for (size_t j = 0; j < v.size(); ++j) {
      if ((i == j) != fidl::Equals(v[i], v[j])) {
        printf("fidl::Equals incorrect for %zu and %zu\n", i, j);
        return false;
      }
      if ((i != j) != !fidl::Equals(v[i], v[j])) {
        printf("fidl::Equals incorrect for %zu and %zu\n", i, j);
        return false;
      }
    }
  }
  return true;
}

TEST(FidlTest, BitsComparison) {
  std::vector<Uint32Bits> bits;
  bits.push_back(Uint32Bits::ONE);
  bits.push_back(Uint32Bits::TWO);
  EXPECT_TRUE(CheckComparison(bits));
}

TEST(FidlTest, SimpleStructComparison) {
  // Create a vector of structs.
  std::vector<Int64Struct> structs;
  for (int32_t i = 1; i < 3; ++i) {
    structs.push_back(Int64Struct{i});
  }
  EXPECT_TRUE(CheckComparison(structs));
}

TEST(FidlTest, SimpleTableComparison) {
  // Create a vector of tables.
  std::vector<SimpleTable> tables;
  for (int32_t i = 1; i < 3; ++i) {
    SimpleTable t;
    t.set_x(i);
    t.set_y(-i);
    tables.push_back(std::move(t));
  }
  EXPECT_TRUE(CheckComparison(tables));
}

TEST(FidlTest, StructWithNullComparison) {
  // Create a vector of structs.
  std::vector<HasOptionalFieldStruct> structs;
  for (int32_t i = 0; i < 3; ++i) {
    structs.push_back(
        HasOptionalFieldStruct{i == 0 ? nullptr : fidl::MakeOptional(Int64Struct({i}))});
  }
  EXPECT_TRUE(CheckComparison(structs));
}

TEST(FidlTest, StructWithMultipleFieldsComparison) {
  // Create a vector of structs.
  std::vector<Has2OptionalFieldStruct> structs;
  for (int32_t i = 0; i < 3; i++) {
    for (int32_t j = 0; j < 3; j++) {
      structs.push_back(
          Has2OptionalFieldStruct{i == 0 ? nullptr : fidl::MakeOptional(Int64Struct({i})),
                                  j == 0 ? nullptr : fidl::MakeOptional(Int64Struct({j}))});
    }
  }
  EXPECT_TRUE(CheckComparison(structs));
}

TEST(FidlTest, UnionComparison) {
  // Create a vector of unions.
  std::vector<SimpleUnion> unions;
  SimpleUnion s;
  s.set_i32(0);
  unions.push_back(std::move(s));
  s.set_i32(1);
  unions.push_back(std::move(s));
  s.set_i64(0);
  unions.push_back(std::move(s));
  s.set_i64(1);
  unions.push_back(std::move(s));
  s.set_s({0});
  unions.push_back(std::move(s));
  s.set_s({1});
  unions.push_back(std::move(s));

  EXPECT_TRUE(CheckComparison(unions));
}

TEST(FidlTest, UnionAssignment) {
  SimpleUnion u;
  u.str() = "foo";
  EXPECT_EQ(u.str(), "foo");
  u.i32() = 3;
  EXPECT_EQ(u.i32(), 3);
}

// Build a vector of fidl::VectorPtr, lexicographically sorted given that
// generator generates values ordered by the given index.
template <typename A>
std::vector<fidl::VectorPtr<A>> BuildSortedVector(size_t size,
                                                  const fit::function<A(int32_t)>& generator) {
  constexpr int32_t kNbBaseElement = 3;

  std::vector<fidl::VectorPtr<A>> result;
  result.push_back(fidl::VectorPtr<A>());
  result.push_back(fidl::VectorPtr<A>(std::vector<A>()));
  if (size == 0) {
    return result;
  }
  auto previous = BuildSortedVector(size - 1, generator);
  for (int32_t i = 0; i < kNbBaseElement; ++i) {
    for (const auto& vector : previous) {
      if (!vector) {
        continue;
      }
      std::vector<A> new_vector;
      new_vector.push_back(generator(i));
      for (const auto& value : *vector) {
        new_vector.push_back(fidl::Clone(value));
      }
      result.push_back(std::move(new_vector));
    }
  }
  return result;
}

TEST(FidlTest, TestBuildSortedVector) {
  EXPECT_EQ(2u, BuildSortedVector<uint64_t>(0, [](uint64_t i) { return i; }).size());
  EXPECT_EQ(5u, BuildSortedVector<uint64_t>(1, [](uint64_t i) { return i; }).size());
  EXPECT_EQ(14u, BuildSortedVector<uint64_t>(2, [](uint64_t i) { return i; }).size());
}

TEST(FidlTest, VectorOfIntComparison) {
  // Create a vector of vectors.
  auto vectors = BuildSortedVector<uint32_t>(3, [](uint32_t i) { return i; });
  EXPECT_TRUE(CheckComparison(vectors));
}

TEST(FidlTest, VectorOfStructComparison) {
  // Create a vector of vectors.
  auto vectors = BuildSortedVector<Int64Struct>(3, [](int32_t i) { return Int64Struct{i}; });
  EXPECT_TRUE(CheckComparison(vectors));
}

TEST(FidlTest, VectorOfOptionalStructComparison) {
  // Create a vector of vectors.
  auto vectors = BuildSortedVector<Int64StructPtr>(3, [](int32_t i) {
    return i == 0 ? Int64StructPtr() : fidl::MakeOptional(Int64Struct({i}));
  });
  EXPECT_TRUE(CheckComparison(vectors));
}

// Build a vector of arrays containing distinct values.
template <typename A>
std::vector<std::array<A, 3>> BuildArray(fit::function<A(int32_t)> generator) {
  std::vector<std::array<A, 3>> arrays;
  for (int32_t i = 0; i < 3; ++i) {
    for (int32_t j = 0; j < 3; ++j) {
      for (int32_t k = 0; k < 3; ++k) {
        std::array<A, 3> array;
        array[0] = generator(i);
        array[1] = generator(j);
        array[2] = generator(k);
        arrays.push_back(std::move(array));
      }
    }
  }
  return arrays;
}

TEST(FidlTest, ArrayOfIntComparison) {
  // Create an vector of arrays.
  auto arrays = BuildArray<int32_t>([](int32_t i) { return i; });
  EXPECT_TRUE(CheckComparison(arrays));
}

TEST(FidlTest, ArrayOfStructComparison) {
  // Create an vector of arrays.
  auto arrays = BuildArray<Int64Struct>([](int32_t i) { return Int64Struct{i}; });
  EXPECT_TRUE(CheckComparison(arrays));
}

TEST(FidlTest, ArrayOfOptionalStructComparison) {
  // Create an vector of arrays.
  auto arrays = BuildArray<Int64StructPtr>(
      [](int32_t i) { return i == 0 ? Int64StructPtr() : fidl::MakeOptional(Int64Struct({i})); });
  EXPECT_TRUE(CheckComparison(arrays));
}

}  // namespace
}  // namespace misc
}  // namespace test
}  // namespace fidl
