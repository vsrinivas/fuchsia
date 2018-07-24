// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

class MaxBytesLibrary : public TestLibrary {
public:
    MaxBytesLibrary() : TestLibrary("max_bytes.fidl", R"FIDL(
library fidl.test.maxbytes;

struct OneBool {
  bool b;
};

struct OptionalOneBool {
  OneBool? s;
};

struct TwoBools {
  bool a;
  bool b;
};

struct OptionalTwoBools {
  TwoBools? s;
};

struct BoolAndU32 {
  bool b;
  uint32 u;
};

struct OptionalBoolAndU32 {
  BoolAndU32? s;
};

struct BoolAndU64 {
  bool b;
  uint64 u;
};

struct OptionalBoolAndU64 {
  BoolAndU64? s;
};

union UnionOfThings {
  OneBool ob;
  BoolAndU64 bu;
};

struct OptionalUnion {
  UnionOfThings? u;
};

struct PaddedVector {
  vector<int32>:3 pv;
};

struct UnboundedVector {
  vector<int32> uv;
};

struct UnboundedVectors {
  vector<int32> uv1;
  vector<int32> uv2;
};

struct ShortString {
  string:5 s;
};

struct UnboundedString {
  string s;
};

struct AnArray {
  array<int64>:5 a;
};

)FIDL") {}
};


static bool simple_structs(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto one_bool = test_library.LookupStruct("OneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.Size(), 1);
    EXPECT_EQ(one_bool->typeshape.MaxOutOfLine(), 0);

    auto two_bools = test_library.LookupStruct("TwoBools");
    EXPECT_NONNULL(two_bools);
    EXPECT_EQ(two_bools->typeshape.Size(), 2);
    EXPECT_EQ(two_bools->typeshape.MaxOutOfLine(), 0);

    auto bool_and_u32 = test_library.LookupStruct("BoolAndU32");
    EXPECT_NONNULL(bool_and_u32);
    EXPECT_EQ(bool_and_u32->typeshape.Size(), 8);
    EXPECT_EQ(bool_and_u32->typeshape.MaxOutOfLine(), 0);

    auto bool_and_u64 = test_library.LookupStruct("BoolAndU64");
    EXPECT_NONNULL(bool_and_u64);
    EXPECT_EQ(bool_and_u64->typeshape.Size(), 16);
    EXPECT_EQ(bool_and_u64->typeshape.MaxOutOfLine(), 0);

    END_TEST;
}

static bool optional_structs(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto one_bool = test_library.LookupStruct("OptionalOneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.Size(), 8);
    EXPECT_EQ(one_bool->typeshape.MaxOutOfLine(), 8);

    auto two_bools = test_library.LookupStruct("OptionalTwoBools");
    EXPECT_NONNULL(two_bools);
    EXPECT_EQ(two_bools->typeshape.Size(), 8);
    EXPECT_EQ(two_bools->typeshape.MaxOutOfLine(), 8);

    auto bool_and_u32 = test_library.LookupStruct("OptionalBoolAndU32");
    EXPECT_NONNULL(bool_and_u32);
    EXPECT_EQ(bool_and_u32->typeshape.Size(), 8);
    EXPECT_EQ(bool_and_u32->typeshape.MaxOutOfLine(), 8);

    auto bool_and_u64 = test_library.LookupStruct("OptionalBoolAndU64");
    EXPECT_NONNULL(bool_and_u64);
    EXPECT_EQ(bool_and_u64->typeshape.Size(), 8);
    EXPECT_EQ(bool_and_u64->typeshape.MaxOutOfLine(), 16);

    END_TEST;
}

static bool unions(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto a_union = test_library.LookupUnion("UnionOfThings");
    EXPECT_NONNULL(a_union);
    EXPECT_EQ(a_union->typeshape.Size(), 24);
    EXPECT_EQ(a_union->typeshape.MaxOutOfLine(), 0);

    auto optional_union = test_library.LookupStruct("OptionalUnion");
    EXPECT_NONNULL(optional_union);
    EXPECT_EQ(optional_union->typeshape.Size(), 8);
    EXPECT_EQ(optional_union->typeshape.MaxOutOfLine(), 24);

    END_TEST;
}

static bool vectors(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto padded_vector = test_library.LookupStruct("PaddedVector");
    EXPECT_NONNULL(padded_vector);
    EXPECT_EQ(padded_vector->typeshape.Size(), 16);
    EXPECT_EQ(padded_vector->typeshape.MaxOutOfLine(), 16);

    auto unbounded_vector = test_library.LookupStruct("UnboundedVector");
    EXPECT_NONNULL(unbounded_vector);
    EXPECT_EQ(unbounded_vector->typeshape.Size(), 16);
    EXPECT_EQ(unbounded_vector->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    auto unbounded_vectors = test_library.LookupStruct("UnboundedVectors");
    EXPECT_NONNULL(unbounded_vectors);
    EXPECT_EQ(unbounded_vectors->typeshape.Size(), 32);
    EXPECT_EQ(unbounded_vectors->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    END_TEST;
}

static bool strings(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto short_string = test_library.LookupStruct("ShortString");
    EXPECT_NONNULL(short_string);
    EXPECT_EQ(short_string->typeshape.Size(), 16);
    EXPECT_EQ(short_string->typeshape.MaxOutOfLine(), 8);

    auto unbounded_string = test_library.LookupStruct("UnboundedString");
    EXPECT_NONNULL(unbounded_string);
    EXPECT_EQ(unbounded_string->typeshape.Size(), 16);
    EXPECT_EQ(unbounded_string->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    END_TEST;
}

static bool arrays(void) {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Parse());

    auto an_array = test_library.LookupStruct("AnArray");
    EXPECT_NONNULL(an_array);
    EXPECT_EQ(an_array->typeshape.Size(), 40);
    EXPECT_EQ(an_array->typeshape.MaxOutOfLine(), 0);

    END_TEST;
}


} // namespace

BEGIN_TEST_CASE(max_bytes_tests);
RUN_TEST(simple_structs);
RUN_TEST(optional_structs);
RUN_TEST(unions);
RUN_TEST(vectors);
RUN_TEST(strings);
RUN_TEST(arrays);
END_TEST_CASE(max_bytes_tests);
