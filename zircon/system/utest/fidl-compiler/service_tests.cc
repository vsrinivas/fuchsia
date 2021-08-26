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

TEST(ServiceTests, GoodEmptyService) {
  TestLibrary library(R"FIDL(
library example;

service SomeService {};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 0);
}

TEST(ServiceTests, GoodService) {
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
  ASSERT_COMPILED_AND_CONVERT(library);

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 3);
  const auto& member0 = service->members[0];
  EXPECT_STR_EQ(std::string(member0.name.data()).c_str(), "some_protocol_first_first");
  EXPECT_STR_EQ(fidl::NameFlatName(fidl::flat::GetName(member0.type_ctor)).c_str(),
                "example/SomeProtocol1");
  const auto& member1 = service->members[1];
  EXPECT_STR_EQ(std::string(member1.name.data()).c_str(), "some_protocol_first_second");
  EXPECT_STR_EQ(fidl::NameFlatName(fidl::flat::GetName(member1.type_ctor)).c_str(),
                "example/SomeProtocol1");
  const auto& member2 = service->members[2];
  EXPECT_STR_EQ(std::string(member2.name.data()).c_str(), "some_protocol_second");
  EXPECT_STR_EQ(fidl::NameFlatName(fidl::flat::GetName(member2.type_ctor)).c_str(),
                "example/SomeProtocol2");
}

TEST(ServiceTests, BadCannotHaveConflictingMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol1 {};
protocol SomeProtocol2 {};

service SomeService {
    this_will_conflict client_end:SomeProtocol1;
    this_will_conflict client_end:SomeProtocol2;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateServiceMemberName);
}

TEST(ServiceTests, BadNoNullableProtocolMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

service SomeService {
    members_are_optional_already client_end:<SomeProtocol, optional>;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableServiceMember);
}

TEST(ServiceTests, BadOnlyProtocolMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type NotAProtocol = struct {};

service SomeService {
    not_a_protocol NotAProtocol;
};

)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): a separate error is used, since client/server ends
  // are types.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeTransportSide);
}

TEST(ServiceTests, BadCannotUseServicesInDecls) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

service SomeService {};

type CannotUseService = struct {
    svc SomeService;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotUseService);
}

}  // namespace
