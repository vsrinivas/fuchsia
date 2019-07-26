// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool valid_empty_service() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

service SomeService {};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto service = library.LookupService("SomeService");
  ASSERT_NONNULL(service);

  EXPECT_EQ(service->members.size(), 0);

  END_TEST;
}

bool valid_service() {
  BEGIN_TEST;

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
  ASSERT_NONNULL(service);

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

  END_TEST;
}

bool invalid_cannot_have_conflicting_members() {
  BEGIN_TEST;

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "multiple service members with the same name");

  END_TEST;
}

bool invalid_no_nullable_protocol_members() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

service SomeService {
    SomeProtocol? members_are_optional_already;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "cannot be nullable");

  END_TEST;
}

bool invalid_only_protocol_members() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct NotAProtocol {};

service SomeService {
    NotAProtocol not_a_protocol;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "only protocol members are allowed");

  END_TEST;
}

bool invalid_cannot_use_services_in_decls() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

service SomeService {};

struct CannotUseService {
    SomeService svc;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "cannot use services");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(service_tests)
RUN_TEST(valid_empty_service)
RUN_TEST(valid_service)
RUN_TEST(invalid_cannot_have_conflicting_members)
RUN_TEST(invalid_no_nullable_protocol_members)
RUN_TEST(invalid_only_protocol_members)
RUN_TEST(invalid_cannot_use_services_in_decls)
END_TEST_CASE(service_tests)
