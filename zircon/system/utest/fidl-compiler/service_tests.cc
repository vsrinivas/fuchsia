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
#include "fidl/flat/types.h"
#include "test_library.h"

namespace {

TEST(ServiceTests, GoodEmptyService) {
  TestLibrary library(R"FIDL(library example;

service SomeService {};
)FIDL");
  ASSERT_COMPILED(library);

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 0);
}

TEST(ServiceTests, GoodService) {
  TestLibrary library(R"FIDL(library example;

protocol SomeProtocol1 {};
protocol SomeProtocol2 {};

service SomeService {
    some_protocol_first_first client_end:SomeProtocol1;
    some_protocol_first_second client_end:SomeProtocol1;
    some_protocol_second client_end:SomeProtocol2;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto service = library.LookupService("SomeService");
  ASSERT_NOT_NULL(service);

  EXPECT_EQ(service->members.size(), 3);
  const auto& member0 = service->members[0];
  EXPECT_STR_EQ(std::string(member0.name.data()).c_str(), "some_protocol_first_first");
  const auto* type0 = static_cast<const fidl::flat::TransportSideType*>(member0.type_ctor->type);
  EXPECT_STR_EQ(fidl::NameFlatName(type0->protocol_decl->name).c_str(), "example/SomeProtocol1");
  const auto& member1 = service->members[1];
  EXPECT_STR_EQ(std::string(member1.name.data()).c_str(), "some_protocol_first_second");
  const auto* type1 = static_cast<const fidl::flat::TransportSideType*>(member1.type_ctor->type);
  EXPECT_STR_EQ(fidl::NameFlatName(type1->protocol_decl->name).c_str(), "example/SomeProtocol1");
  const auto& member2 = service->members[2];
  EXPECT_STR_EQ(std::string(member2.name.data()).c_str(), "some_protocol_second");
  const auto* type2 = static_cast<const fidl::flat::TransportSideType*>(member2.type_ctor->type);
  EXPECT_STR_EQ(fidl::NameFlatName(type2->protocol_decl->name).c_str(), "example/SomeProtocol2");
}

TEST(ServiceTests, BadCannotHaveConflictingMembers) {
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol1 {};
protocol SomeProtocol2 {};

service SomeService {
    this_will_conflict client_end:SomeProtocol1;
    this_will_conflict client_end:SomeProtocol2;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateServiceMemberName);
}

TEST(ServiceTests, BadNoNullableProtocolMembers) {
  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

service SomeService {
    members_are_optional_already client_end:<SomeProtocol, optional>;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableServiceMember);
}

TEST(ServiceTests, BadOnlyProtocolMembers) {
  TestLibrary library(R"FIDL(
library example;

type NotAProtocol = struct {};

service SomeService {
    not_a_protocol NotAProtocol;
};

)FIDL");
  // NOTE(fxbug.dev/72924): a separate error is used, since client/server ends
  // are types.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeTransportSide);
}

TEST(ServiceTests, BadCannotUseServicesInDecls) {
  TestLibrary library(R"FIDL(
library example;

service SomeService {};

type CannotUseService = struct {
    svc SomeService;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotUseService);
}

}  // namespace
