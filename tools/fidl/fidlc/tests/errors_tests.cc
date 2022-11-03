// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(ErrorsTests, GoodError) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    Method() -> (struct {
        foo string;
    }) error int32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto methods = &library.LookupProtocol("Example")->methods;
  ASSERT_EQ(methods->size(), 1);
  auto method = &methods->at(0);
  auto response = method->maybe_response.get();
  ASSERT_NOT_NULL(response);

  auto id = static_cast<const fidl::flat::IdentifierType*>(response->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  ASSERT_EQ(as_struct->members.size(), 1);

  auto response_member = &as_struct->members.at(0);
  ASSERT_EQ(response_member->type_ctor->type->kind, fidl::flat::Type::Kind::kIdentifier);
  auto result_identifier =
      static_cast<const fidl::flat::IdentifierType*>(response_member->type_ctor->type);
  const fidl::flat::Union* result_union =
      library.LookupUnion(std::string(result_identifier->name.decl_name()));
  ASSERT_NOT_NULL(result_union);
  ASSERT_NOT_NULL(result_union->attributes);
  ASSERT_TRUE(result_union->attributes->Get("result") != nullptr);
  ASSERT_EQ(result_union->members.size(), 2);

  const auto& success = result_union->members.at(0);
  ASSERT_NOT_NULL(success.maybe_used);
  ASSERT_STREQ("response", std::string(success.maybe_used->name.data()).c_str());

  const fidl::flat::Union::Member& error = result_union->members.at(1);
  ASSERT_NOT_NULL(error.maybe_used);
  ASSERT_STREQ("err", std::string(error.maybe_used->name.data()).c_str());

  ASSERT_NOT_NULL(error.maybe_used->type_ctor->type);
  ASSERT_EQ(error.maybe_used->type_ctor->type->kind, fidl::flat::Type::Kind::kPrimitive);
  auto primitive_type =
      static_cast<const fidl::flat::PrimitiveType*>(error.maybe_used->type_ctor->type);
  ASSERT_EQ(primitive_type->subtype, fidl::types::PrimitiveSubtype::kInt32);
}

TEST(ErrorsTests, GoodErrorUnsigned) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    Method() -> (struct {
        foo string;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ErrorsTests, GoodErrorEmptyStructAsSuccess) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod() -> () error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto protocol = library.LookupProtocol("MyProtocol");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);

  auto& method = protocol->methods[0];
  EXPECT_TRUE(method.has_request);
  EXPECT_NULL(method.maybe_request.get());
  ASSERT_TRUE(method.has_response && method.maybe_response.get());

  auto id = static_cast<const fidl::flat::IdentifierType*>(method.maybe_response->type);
  auto response = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_TRUE(response->kind == fidl::flat::Decl::Kind::kStruct);
  ASSERT_EQ(response->members.size(), 1);
}

TEST(ErrorsTests, GoodErrorEnum) {
  TestLibrary library(R"FIDL(library example;

type ErrorType = enum : int32 {
    GOOD = 1;
    BAD = 2;
    UGLY = 3;
};

protocol Example {
    Method() -> (struct {
        foo string;
    }) error ErrorType;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ErrorsTests, GoodErrorEnumAfter) {
  TestLibrary library(R"FIDL(library example;

protocol Example {
    Method() -> (struct {
        foo string;
    }) error ErrorType;
};

type ErrorType = enum : int32 {
    GOOD = 1;
    BAD = 2;
    UGLY = 3;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ErrorsTests, BadErrorUnknownIdentifier) {
  TestLibrary library;
  library.AddFile("bad/fi-0052.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ParsingError");
}

TEST(ErrorsTests, BadErrorWrongPrimitive) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
    Method() -> (struct { foo string; }) error float32;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidErrorType);
}

TEST(ErrorsTests, BadErrorMissingType) {
  TestLibrary library(R"FIDL(
library example;
protocol Example {
    Method() -> (flub int32) error;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ErrorsTests, BadErrorNotAType) {
  TestLibrary library(R"FIDL(
library example;
protocol Example {
    Method() -> (flub int32) error "hello";
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ErrorsTests, BadErrorNoResponse) {
  TestLibrary library(R"FIDL(
library example;
protocol Example {
    Method() -> error int32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ErrorsTests, BadErrorUnexpectedEndOfFile) {
  TestLibrary library(R"FIDL(
library example;
type ForgotTheSemicolon = table {}
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ErrorsTests, BadIncorrectIdentifier) {
  TestLibrary library;
  library.AddFile("bad/fi-0009.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ErrorsTests, BadErrorEmptyFile) {
  TestLibrary library("");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}
}  // namespace
