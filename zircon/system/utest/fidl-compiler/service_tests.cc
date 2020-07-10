// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(ServiceTests, valid_empty_service) {
  TestLibrary library(R"FIDL(
library example;

service SomeService {};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 0);
}

TEST(ServiceTests, valid_service) {
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol1 {};
protocol SomeProtocol2 {};

service SomeService {
    SomeProtocol1 some_protocol_first_first;
    SomeProtocol1 some_protocol_first_second;
    SomeProtocol2 some_protocol_second;
};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 3);
  const auto& member0 = service->members[0];
  EXPECT_STR_EQ(std::string(member0.name.data()).c_str(), "some_protocol_first_first");
  EXPECT_STR_EQ(fidl::NameFlatName(member0.type_ctor->name).c_str(), "example/SomeProtocol1");
  const auto& member1 = service->members[1];
  EXPECT_STR_EQ(std::string(member1.name.data()).c_str(), "some_protocol_first_second");
  EXPECT_STR_EQ(fidl::NameFlatName(member1.type_ctor->name).c_str(), "example/SomeProtocol1");
  const auto& member2 = service->members[2];
  EXPECT_STR_EQ(std::string(member2.name.data()).c_str(), "some_protocol_second");
  EXPECT_STR_EQ(fidl::NameFlatName(member2.type_ctor->name).c_str(), "example/SomeProtocol2");
}

TEST(ServiceTests, invalid_cannot_have_conflicting_members) {
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol1 {};
protocol SomeProtocol2 {};

service SomeService {
    SomeProtocol1 this_will_conflict;
    SomeProtocol2 this_will_conflict;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateServiceMemberName);
}

TEST(ServiceTests, invalid_no_nullable_protocol_members) {
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

service SomeService {
    SomeProtocol? members_are_optional_already;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNullableServiceMember);
}

TEST(ServiceTests, invalid_only_protocol_members) {
  TestLibrary library(R"FIDL(
library example;

struct NotAProtocol {};

service SomeService {
    NotAProtocol not_a_protocol;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNonProtocolServiceMember);
}

TEST(ServiceTests, invalid_cannot_use_services_in_decls) {
  TestLibrary library(R"FIDL(
library example;

service SomeService {};

struct CannotUseService {
    SomeService svc;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotUseServicesInOtherDeclarations);
}

}  // namespace
