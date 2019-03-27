// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

// TODO(FIDL-458): Merge with max_handle_tests.

namespace {

class MaxBytesLibrary : public TestLibrary {
public:
    MaxBytesLibrary()
        : TestLibrary("max_bytes.fidl", R"FIDL(
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

table TableWithNoMembers {
};

table TableWithOneBool {
  1: bool b;
};

table TableWithOptionalOneBool {
  1: OneBool s;
};

table TableWithOptionalTableWithOneBool {
  1: TableWithOneBool s;
};

table TableWithTwoBools {
  1: bool a;
  2: bool b;
};

table TableWithOptionalTwoBools {
  1: TwoBools s;
};

table TableWithOptionalTableWithTwoBools {
  1: TableWithTwoBools s;
};

table TableWithBoolAndU32 {
  1: bool b;
  2: uint32 u;
};

table TableWithOptionalBoolAndU32 {
  1: BoolAndU32 s;
};

table TableWithOptionalTableWithBoolAndU32 {
  1: TableWithBoolAndU32 s;
};

table TableWithBoolAndU64 {
  1: bool b;
  2: uint64 u;
};

table TableWithOptionalBoolAndU64 {
  1: BoolAndU64 s;
};

table TableWithOptionalTableWithBoolAndU64 {
  1: TableWithBoolAndU64 s;
};

table TableWithOptionalUnion {
  1: UnionOfThings u;
};

table TableWithPaddedVector {
  1: vector<int32>:3 pv;
};

table TableWithUnboundedVector {
  1: vector<int32> uv;
};

table TableWithUnboundedVectors {
  1: vector<int32> uv1;
  2: vector<int32> uv2;
};

table TableWithShortString {
  1: string:5 s;
};

table TableWithUnboundedString {
  1: string s;
};

table TableWithAnArray {
  1: array<int64>:5 a;
};

xunion EmptyXUnion {
};

xunion XUnionWithOneBool {
  bool b;
};

xunion XUnionWithBoolAndU32 {
  bool b;
  uint32 u;
};

xunion XUnionWithBoundedOutOfLineObject {
  // smaller than |v| below, so will not be selected for max-out-of-line
  // calculation.
  bool b;

  // 1. vector<int32>:5 = 20 bytes
  //                    = 24 bytes for 8-byte boundary alignment
  //                    +  8 bytes for vector element count
  //                    +  8 bytes for data pointer
  //                    = 40 bytes total
  // 1. vector<vector<int32>:5>:6 = vector<int32>:5 (40) * 6
  //                              = 240 bytes
  //                              +   8 bytes for vector element count
  //                              +   8 bytes for data pointer
  //                              = 256 bytes total
  vector<vector<int32>:5>:6 v;
};

xunion XUnionWithUnboundedOutOfLineObject {
  string s;
};

struct StructWithOptionalEmptyXUnion {
  EmptyXUnion? opt_empty;
};

protocol SomeProtocol {};

struct UsingSomeProtocol {
  SomeProtocol value;
};

struct UsingOptSomeProtocol {
  SomeProtocol? value;
};

struct UsingRequestSomeProtocol {
  request<SomeProtocol> value;
};

struct UsingOptRequestSomeProtocol {
  request<SomeProtocol>? value;
};

)FIDL") {}
};

static bool simple_structs() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

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

static bool simple_tables() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto no_members = test_library.LookupTable("TableWithNoMembers");
    EXPECT_NONNULL(no_members);
    EXPECT_EQ(no_members->typeshape.Size(), 16);
    EXPECT_EQ(no_members->typeshape.MaxOutOfLine(), 0);

    auto one_bool = test_library.LookupTable("TableWithOneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.Size(), 16);
    EXPECT_EQ(one_bool->typeshape.MaxOutOfLine(), 24);

    auto two_bools = test_library.LookupTable("TableWithTwoBools");
    EXPECT_NONNULL(two_bools);
    EXPECT_EQ(two_bools->typeshape.Size(), 16);
    EXPECT_EQ(two_bools->typeshape.MaxOutOfLine(), 48);

    auto bool_and_u32 = test_library.LookupTable("TableWithBoolAndU32");
    EXPECT_NONNULL(bool_and_u32);
    EXPECT_EQ(bool_and_u32->typeshape.Size(), 16);
    EXPECT_EQ(bool_and_u32->typeshape.MaxOutOfLine(), 48);

    auto bool_and_u64 = test_library.LookupTable("TableWithBoolAndU64");
    EXPECT_NONNULL(bool_and_u64);
    EXPECT_EQ(bool_and_u64->typeshape.Size(), 16);
    EXPECT_EQ(bool_and_u64->typeshape.MaxOutOfLine(), 48);

    END_TEST;
}

static bool optional_structs() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

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

static bool optional_tables() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto one_bool = test_library.LookupTable("TableWithOptionalOneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.Size(), 16);
    EXPECT_EQ(one_bool->typeshape.MaxOutOfLine(), 24);

    auto table_with_one_bool = test_library.LookupTable("TableWithOptionalTableWithOneBool");
    EXPECT_NONNULL(table_with_one_bool);
    EXPECT_EQ(table_with_one_bool->typeshape.Size(), 16);
    EXPECT_EQ(table_with_one_bool->typeshape.MaxOutOfLine(), 56);

    auto two_bools = test_library.LookupTable("TableWithOptionalTwoBools");
    EXPECT_NONNULL(two_bools);
    EXPECT_EQ(two_bools->typeshape.Size(), 16);
    EXPECT_EQ(two_bools->typeshape.MaxOutOfLine(), 24);

    auto table_with_two_bools = test_library.LookupTable("TableWithOptionalTableWithTwoBools");
    EXPECT_NONNULL(table_with_two_bools);
    EXPECT_EQ(table_with_two_bools->typeshape.Size(), 16);
    EXPECT_EQ(table_with_two_bools->typeshape.MaxOutOfLine(), 80);

    auto bool_and_u32 = test_library.LookupTable("TableWithOptionalBoolAndU32");
    EXPECT_NONNULL(bool_and_u32);
    EXPECT_EQ(bool_and_u32->typeshape.Size(), 16);
    EXPECT_EQ(bool_and_u32->typeshape.MaxOutOfLine(), 24);

    auto table_with_bool_and_u32 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU32");
    EXPECT_NONNULL(table_with_bool_and_u32);
    EXPECT_EQ(table_with_bool_and_u32->typeshape.Size(), 16);
    EXPECT_EQ(table_with_bool_and_u32->typeshape.MaxOutOfLine(), 80);

    auto bool_and_u64 = test_library.LookupTable("TableWithOptionalBoolAndU64");
    EXPECT_NONNULL(bool_and_u64);
    EXPECT_EQ(bool_and_u64->typeshape.Size(), 16);
    EXPECT_EQ(bool_and_u64->typeshape.MaxOutOfLine(), 32);

    auto table_with_bool_and_u64 = test_library.LookupTable("TableWithOptionalTableWithBoolAndU64");
    EXPECT_NONNULL(table_with_bool_and_u64);
    EXPECT_EQ(table_with_bool_and_u64->typeshape.Size(), 16);
    EXPECT_EQ(table_with_bool_and_u64->typeshape.MaxOutOfLine(), 80);

    END_TEST;
}

static bool unions() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto a_union = test_library.LookupUnion("UnionOfThings");
    EXPECT_NONNULL(a_union);
    EXPECT_EQ(a_union->typeshape.Size(), 24);
    EXPECT_EQ(a_union->typeshape.MaxOutOfLine(), 0);

    auto optional_union = test_library.LookupStruct("OptionalUnion");
    EXPECT_NONNULL(optional_union);
    EXPECT_EQ(optional_union->typeshape.Size(), 8);
    EXPECT_EQ(optional_union->typeshape.MaxOutOfLine(), 24);

    auto table_with_optional_union = test_library.LookupTable("TableWithOptionalUnion");
    EXPECT_NONNULL(table_with_optional_union);
    EXPECT_EQ(table_with_optional_union->typeshape.Size(), 16);
    EXPECT_EQ(table_with_optional_union->typeshape.MaxOutOfLine(), 40);

    END_TEST;
}

static bool vectors() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

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

    auto table_with_padded_vector = test_library.LookupTable("TableWithPaddedVector");
    EXPECT_NONNULL(table_with_padded_vector);
    EXPECT_EQ(table_with_padded_vector->typeshape.Size(), 16);
    EXPECT_EQ(table_with_padded_vector->typeshape.MaxOutOfLine(), 48);

    auto table_with_unbounded_vector = test_library.LookupTable("TableWithUnboundedVector");
    EXPECT_NONNULL(table_with_unbounded_vector);
    EXPECT_EQ(table_with_unbounded_vector->typeshape.Size(), 16);
    EXPECT_EQ(table_with_unbounded_vector->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    auto table_with_unbounded_vectors = test_library.LookupTable("TableWithUnboundedVectors");
    EXPECT_NONNULL(table_with_unbounded_vectors);
    EXPECT_EQ(table_with_unbounded_vectors->typeshape.Size(), 16);
    EXPECT_EQ(table_with_unbounded_vectors->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    END_TEST;
}

static bool strings() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto short_string = test_library.LookupStruct("ShortString");
    EXPECT_NONNULL(short_string);
    EXPECT_EQ(short_string->typeshape.Size(), 16);
    EXPECT_EQ(short_string->typeshape.MaxOutOfLine(), 8);

    auto unbounded_string = test_library.LookupStruct("UnboundedString");
    EXPECT_NONNULL(unbounded_string);
    EXPECT_EQ(unbounded_string->typeshape.Size(), 16);
    EXPECT_EQ(unbounded_string->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    auto table_with_short_string = test_library.LookupTable("TableWithShortString");
    EXPECT_NONNULL(table_with_short_string);
    EXPECT_EQ(table_with_short_string->typeshape.Size(), 16);
    EXPECT_EQ(table_with_short_string->typeshape.MaxOutOfLine(), 40);

    auto table_with_unbounded_string = test_library.LookupTable("TableWithUnboundedString");
    EXPECT_NONNULL(table_with_unbounded_string);
    EXPECT_EQ(table_with_unbounded_string->typeshape.Size(), 16);
    EXPECT_EQ(table_with_unbounded_string->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    END_TEST;
}

static bool arrays() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto an_array = test_library.LookupStruct("AnArray");
    EXPECT_NONNULL(an_array);
    EXPECT_EQ(an_array->typeshape.Size(), 40);
    EXPECT_EQ(an_array->typeshape.MaxOutOfLine(), 0);

    auto table_with_an_array = test_library.LookupTable("TableWithAnArray");
    EXPECT_NONNULL(table_with_an_array);
    EXPECT_EQ(table_with_an_array->typeshape.Size(), 16);
    EXPECT_EQ(table_with_an_array->typeshape.MaxOutOfLine(), 56);

    END_TEST;
}

static bool xunions() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto empty = test_library.LookupXUnion("EmptyXUnion");
    EXPECT_EQ(empty->typeshape.Size(), 24);
    EXPECT_EQ(empty->typeshape.MaxOutOfLine(), 0);

    auto one_bool = test_library.LookupXUnion("XUnionWithOneBool");
    EXPECT_EQ(one_bool->typeshape.Size(), 24);
    EXPECT_EQ(one_bool->typeshape.MaxOutOfLine(), 8);

    auto xu = test_library.LookupXUnion("XUnionWithBoundedOutOfLineObject");
    EXPECT_EQ(xu->typeshape.Size(), 24);
    EXPECT_EQ(xu->typeshape.MaxOutOfLine(), 256);

    auto unbounded = test_library.LookupXUnion("XUnionWithUnboundedOutOfLineObject");
    EXPECT_EQ(unbounded->typeshape.Size(), 24);
    EXPECT_EQ(unbounded->typeshape.MaxOutOfLine(), std::numeric_limits<uint32_t>::max());

    auto opt_empty = test_library.LookupStruct("StructWithOptionalEmptyXUnion");
    EXPECT_EQ(opt_empty->typeshape.Size(), 24);
    EXPECT_EQ(opt_empty->typeshape.MaxOutOfLine(), 0);

    END_TEST;
}

bool protocols_and_request_of_protocols() {
    BEGIN_TEST;

    MaxBytesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto using_some_protocol = test_library.LookupStruct("UsingSomeProtocol");
    EXPECT_NONNULL(using_some_protocol);
    EXPECT_EQ(using_some_protocol->typeshape.Size(), 4);
    EXPECT_EQ(using_some_protocol->typeshape.Alignment(), 4);
    EXPECT_EQ(using_some_protocol->typeshape.MaxOutOfLine(), 0);

    auto using_opt_some_protocol = test_library.LookupStruct("UsingOptSomeProtocol");
    EXPECT_NONNULL(using_opt_some_protocol);
    EXPECT_EQ(using_opt_some_protocol->typeshape.Size(), 4);
    EXPECT_EQ(using_opt_some_protocol->typeshape.Alignment(), 4);
    EXPECT_EQ(using_opt_some_protocol->typeshape.MaxOutOfLine(), 0);

    auto using_request_some_protocol = test_library.LookupStruct("UsingRequestSomeProtocol");
    EXPECT_NONNULL(using_request_some_protocol);
    EXPECT_EQ(using_request_some_protocol->typeshape.Size(), 4);
    EXPECT_EQ(using_request_some_protocol->typeshape.Alignment(), 4);
    EXPECT_EQ(using_request_some_protocol->typeshape.MaxOutOfLine(), 0);

    auto using_opt_request_some_protocol = test_library.LookupStruct("UsingOptRequestSomeProtocol");
    EXPECT_NONNULL(using_opt_request_some_protocol);
    EXPECT_EQ(using_opt_request_some_protocol->typeshape.Size(), 4);
    EXPECT_EQ(using_opt_request_some_protocol->typeshape.Alignment(), 4);
    EXPECT_EQ(using_opt_request_some_protocol->typeshape.MaxOutOfLine(), 0);

    END_TEST;
}

bool recursive_request() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  request<MessagePort> message_port_req;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  EXPECT_NONNULL(web_message);
  EXPECT_EQ(web_message->typeshape.Size(), 4);
  EXPECT_EQ(web_message->typeshape.Alignment(), 4);
  EXPECT_EQ(web_message->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(web_message->typeshape.MaxHandles(), 1);
  EXPECT_EQ(web_message->typeshape.Depth(), 0);

  auto message_port = library.LookupInterface("MessagePort");
  EXPECT_NONNULL(message_port);
  EXPECT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  EXPECT_NONNULL(post_message_request);
  EXPECT_EQ(post_message_request->typeshape.Size(), 24);
  EXPECT_EQ(post_message_request->typeshape.Alignment(), 8);
  EXPECT_EQ(post_message_request->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(post_message_request->typeshape.MaxHandles(), 1);
  EXPECT_EQ(post_message_request->typeshape.Depth(), 0);

  END_TEST;
}

bool recursive_opt_request() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  request<MessagePort>? opt_message_port_req;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  EXPECT_NONNULL(web_message);
  EXPECT_EQ(web_message->typeshape.Size(), 4);
  EXPECT_EQ(web_message->typeshape.Alignment(), 4);
  EXPECT_EQ(web_message->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(web_message->typeshape.MaxHandles(), 1);
  EXPECT_EQ(web_message->typeshape.Depth(), 0);

  auto message_port = library.LookupInterface("MessagePort");
  EXPECT_NONNULL(message_port);
  EXPECT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  EXPECT_NONNULL(post_message_request);
  EXPECT_EQ(post_message_request->typeshape.Size(), 24);
  EXPECT_EQ(post_message_request->typeshape.Alignment(), 8);
  EXPECT_EQ(post_message_request->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(post_message_request->typeshape.MaxHandles(), 1);
  EXPECT_EQ(post_message_request->typeshape.Depth(), 0);

  END_TEST;
}

bool recursive_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  MessagePort message_port;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  EXPECT_NONNULL(web_message);
  EXPECT_EQ(web_message->typeshape.Size(), 4);
  EXPECT_EQ(web_message->typeshape.Alignment(), 4);
  EXPECT_EQ(web_message->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(web_message->typeshape.MaxHandles(), 1);
  EXPECT_EQ(web_message->typeshape.Depth(), 0);

  auto message_port = library.LookupInterface("MessagePort");
  EXPECT_NONNULL(message_port);
  EXPECT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  EXPECT_NONNULL(post_message_request);
  EXPECT_EQ(post_message_request->typeshape.Size(), 24);
  EXPECT_EQ(post_message_request->typeshape.Alignment(), 8);
  EXPECT_EQ(post_message_request->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(post_message_request->typeshape.MaxHandles(), 1);
  EXPECT_EQ(post_message_request->typeshape.Depth(), 0);

  END_TEST;
}

bool recursive_opt_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct WebMessage {
  MessagePort? opt_message_port;
};

protocol MessagePort {
  PostMessage(WebMessage message) -> (bool success);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto web_message = library.LookupStruct("WebMessage");
  EXPECT_NONNULL(web_message);
  EXPECT_EQ(web_message->typeshape.Size(), 4);
  EXPECT_EQ(web_message->typeshape.Alignment(), 4);
  EXPECT_EQ(web_message->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(web_message->typeshape.MaxHandles(), 1);
  EXPECT_EQ(web_message->typeshape.Depth(), 0);

  auto message_port = library.LookupInterface("MessagePort");
  EXPECT_NONNULL(message_port);
  EXPECT_EQ(message_port->methods.size(), 1);
  auto& post_message = message_port->methods[0];
  auto post_message_request = post_message.maybe_request;
  EXPECT_NONNULL(post_message_request);
  EXPECT_EQ(post_message_request->typeshape.Size(), 24);
  EXPECT_EQ(post_message_request->typeshape.Alignment(), 8);
  EXPECT_EQ(post_message_request->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(post_message_request->typeshape.MaxHandles(), 1);
  EXPECT_EQ(post_message_request->typeshape.Depth(), 0);

  END_TEST;
}

bool recursive_struct() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct TheStruct {
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  EXPECT_NONNULL(the_struct);
  EXPECT_EQ(the_struct->typeshape.Size(), 8);
  EXPECT_EQ(the_struct->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(the_struct->typeshape.MaxOutOfLine(), 0);
  // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
  EXPECT_EQ(the_struct->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(the_struct->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  END_TEST;
}

bool recursive_struct_with_handles() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct TheStruct {
  handle<vmo> some_handle;
  TheStruct? opt_one_more;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto the_struct = library.LookupStruct("TheStruct");
  EXPECT_NONNULL(the_struct);
  EXPECT_EQ(the_struct->typeshape.Size(), 16);
  EXPECT_EQ(the_struct->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(the_struct->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(the_struct->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(the_struct->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  END_TEST;
}

bool co_recursive_struct() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct A {
    B? foo;
};

struct B {
    A? bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_a = library.LookupStruct("A");
  EXPECT_NONNULL(struct_a);
  EXPECT_EQ(struct_a->typeshape.Size(), 8);
  EXPECT_EQ(struct_a->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_a->typeshape.MaxOutOfLine(), 16);
  // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
  EXPECT_EQ(struct_a->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_a->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  auto struct_b = library.LookupStruct("B");
  EXPECT_NONNULL(struct_b);
  EXPECT_EQ(struct_b->typeshape.Size(), 8);
  EXPECT_EQ(struct_b->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_b->typeshape.MaxOutOfLine(), 8);
  // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
  EXPECT_EQ(struct_b->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_b->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  END_TEST;
}

bool co_recursive_struct_with_handles() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct A {
    handle a;
    B? foo;
};

struct B {
    handle b;
    A? bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_a = library.LookupStruct("A");
  EXPECT_NONNULL(struct_a);
  EXPECT_EQ(struct_a->typeshape.Size(), 16);
  EXPECT_EQ(struct_a->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_a->typeshape.MaxOutOfLine(), 32);
  EXPECT_EQ(struct_a->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_a->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  auto struct_b = library.LookupStruct("B");
  EXPECT_NONNULL(struct_b);
  EXPECT_EQ(struct_b->typeshape.Size(), 16);
  EXPECT_EQ(struct_b->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_b->typeshape.MaxOutOfLine(), 16);
  EXPECT_EQ(struct_b->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_b->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  END_TEST;
}

bool co_recursive_struct2() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Foo {
    Bar b;
};

struct Bar {
    Foo? f;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto struct_foo = library.LookupStruct("Foo");
  EXPECT_NONNULL(struct_foo);
  EXPECT_EQ(struct_foo->typeshape.Size(), 8);
  EXPECT_EQ(struct_foo->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_foo->typeshape.MaxOutOfLine(), 0);
  // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
  EXPECT_EQ(struct_foo->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_foo->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  auto struct_bar = library.LookupStruct("Bar");
  EXPECT_NONNULL(struct_bar);
  EXPECT_EQ(struct_bar->typeshape.Size(), 8);
  EXPECT_EQ(struct_bar->typeshape.Alignment(), 8);
  // TODO(FIDL-457): Imprecision here, max out-ofline should be infinite.
  EXPECT_EQ(struct_bar->typeshape.MaxOutOfLine(), 0);
  // TODO(FIDL-457): Incorrectly saturating, there are no handles here.
  EXPECT_EQ(struct_bar->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(struct_bar->typeshape.Depth(), std::numeric_limits<uint32_t>::max());

  END_TEST;
}

bool struct_two_deep() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct DiffEntry {
    vector<uint8>:256 key;

    Value? base;
    Value? left;
    Value? right;
};

struct Value {
    Buffer? value;
    Priority priority;
};

struct Buffer {
    handle<vmo> vmo;
    uint64 size;
};

enum Priority {
    EAGER = 0;
    LAZY = 1;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto buffer = library.LookupStruct("Buffer");
  EXPECT_NONNULL(buffer);
  EXPECT_EQ(buffer->typeshape.Size(), 16);
  EXPECT_EQ(buffer->typeshape.Alignment(), 8);
  EXPECT_EQ(buffer->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(buffer->typeshape.MaxHandles(), 1);
  EXPECT_EQ(buffer->typeshape.Depth(), 0);

  auto value = library.LookupStruct("Value");
  EXPECT_NONNULL(value);
  EXPECT_EQ(value->typeshape.Size(), 16);
  EXPECT_EQ(value->typeshape.Alignment(), 8);
  EXPECT_EQ(value->typeshape.MaxOutOfLine(), 16);
  EXPECT_EQ(buffer->typeshape.MaxHandles(), 1);
  EXPECT_EQ(value->typeshape.Depth(), 1);

  auto diff_entry = library.LookupStruct("DiffEntry");
  EXPECT_NONNULL(diff_entry);
  EXPECT_EQ(diff_entry->typeshape.Size(), 40);
  EXPECT_EQ(diff_entry->typeshape.Alignment(), 8);
  EXPECT_EQ(diff_entry->typeshape.MaxOutOfLine(), 352);
  // TODO(FIDL-457): max 3 handles, since each Value has one.
  EXPECT_EQ(buffer->typeshape.MaxHandles(), 1);
  EXPECT_EQ(diff_entry->typeshape.Depth(), 2);

  END_TEST;
}

bool protocol_child_and_parent() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary parent_library("parent.fidl", R"FIDL(
library parent;

[FragileBase]
protocol Parent {
  Sync() -> ();
};
)FIDL", &shared);
  ASSERT_TRUE(parent_library.Compile());

  TestLibrary child_library("child.fidl", R"FIDL(
library child;

using parent;

protocol Child {
  compose parent.Parent;
};
)FIDL", &shared);
  ASSERT_TRUE(child_library.AddDependentLibrary(std::move(parent_library)));
  ASSERT_TRUE(child_library.Compile());

  auto child = child_library.LookupInterface("Child");
  EXPECT_NONNULL(child);
  EXPECT_EQ(child->all_methods.size(), 1);
  auto& sync = child->all_methods[0];
  auto sync_request = sync->maybe_request;
  EXPECT_NONNULL(sync_request);
  EXPECT_EQ(sync_request->typeshape.Size(), 16);
  EXPECT_EQ(sync_request->typeshape.Alignment(), 8);
  EXPECT_EQ(sync_request->typeshape.MaxOutOfLine(), 0);
  EXPECT_EQ(sync_request->typeshape.MaxHandles(), 0);
  EXPECT_EQ(sync_request->typeshape.Depth(), 0);

  END_TEST;
}

} // namespace

BEGIN_TEST_CASE(max_bytes_tests)
RUN_TEST(simple_structs)
RUN_TEST(simple_tables)
RUN_TEST(optional_structs)
RUN_TEST(optional_tables)
RUN_TEST(unions)
RUN_TEST(vectors)
RUN_TEST(strings)
RUN_TEST(arrays)
RUN_TEST(xunions)
RUN_TEST(protocols_and_request_of_protocols)
RUN_TEST(recursive_request)
RUN_TEST(recursive_opt_request)
RUN_TEST(recursive_protocol)
RUN_TEST(recursive_opt_protocol)
RUN_TEST(recursive_struct)
RUN_TEST(recursive_struct_with_handles)
RUN_TEST(co_recursive_struct)
RUN_TEST(co_recursive_struct_with_handles)
RUN_TEST(co_recursive_struct2)
RUN_TEST(struct_two_deep)
RUN_TEST(protocol_child_and_parent)
END_TEST_CASE(max_bytes_tests)
