// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/flat/types.h"
#include "test_library.h"

namespace {

TEST(MethodTests, GoodValidComposeMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasComposeMethod1 {
    compose();
};

open protocol HasComposeMethod2 {
    compose() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasComposeMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasComposeMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidStrictComposeMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasComposeMethod1 {
    strict compose();
};

open protocol HasComposeMethod2 {
    strict compose() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasComposeMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasComposeMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidFlexibleComposeMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasComposeMethod1 {
    flexible compose();
};

open protocol HasComposeMethod2 {
    flexible compose() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasComposeMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasComposeMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidStrictMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasStrictMethod1 {
    strict();
};

open protocol HasStrictMethod2 {
    strict() -> ();
};

open protocol HasStrictMethod3 {
    strict strict();
};

open protocol HasStrictMethod4 {
    strict strict() -> ();
};

open protocol HasStrictMethod5 {
    flexible strict();
};

open protocol HasStrictMethod6 {
    flexible strict() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasStrictMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasStrictMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);

  auto protocol3 = library.LookupProtocol("HasStrictMethod3");
  ASSERT_NOT_NULL(protocol3);
  ASSERT_EQ(protocol3->methods.size(), 1);
  EXPECT_EQ(protocol3->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol3->all_methods.size(), 1);

  auto protocol4 = library.LookupProtocol("HasStrictMethod4");
  ASSERT_NOT_NULL(protocol4);
  ASSERT_EQ(protocol4->methods.size(), 1);
  EXPECT_EQ(protocol4->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol4->all_methods.size(), 1);

  auto protocol5 = library.LookupProtocol("HasStrictMethod5");
  ASSERT_NOT_NULL(protocol5);
  ASSERT_EQ(protocol5->methods.size(), 1);
  EXPECT_EQ(protocol5->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol5->all_methods.size(), 1);

  auto protocol6 = library.LookupProtocol("HasStrictMethod6");
  ASSERT_NOT_NULL(protocol6);
  ASSERT_EQ(protocol6->methods.size(), 1);
  EXPECT_EQ(protocol6->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol6->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidFlexibleTwoWayMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasFlexibleTwoWayMethod1 {
    flexible();
};

open protocol HasFlexibleTwoWayMethod2 {
    flexible() -> ();
};

open protocol HasFlexibleTwoWayMethod3 {
    strict flexible();
};

open protocol HasFlexibleTwoWayMethod4 {
    strict flexible() -> ();
};

open protocol HasFlexibleTwoWayMethod5 {
    flexible flexible();
};

open protocol HasFlexibleTwoWayMethod6 {
    flexible flexible() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasFlexibleTwoWayMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasFlexibleTwoWayMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);

  auto protocol3 = library.LookupProtocol("HasFlexibleTwoWayMethod3");
  ASSERT_NOT_NULL(protocol3);
  ASSERT_EQ(protocol3->methods.size(), 1);
  EXPECT_EQ(protocol3->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol3->all_methods.size(), 1);

  auto protocol4 = library.LookupProtocol("HasFlexibleTwoWayMethod4");
  ASSERT_NOT_NULL(protocol4);
  ASSERT_EQ(protocol4->methods.size(), 1);
  EXPECT_EQ(protocol4->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol4->all_methods.size(), 1);

  auto protocol5 = library.LookupProtocol("HasFlexibleTwoWayMethod5");
  ASSERT_NOT_NULL(protocol5);
  ASSERT_EQ(protocol5->methods.size(), 1);
  EXPECT_EQ(protocol5->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol5->all_methods.size(), 1);

  auto protocol6 = library.LookupProtocol("HasFlexibleTwoWayMethod6");
  ASSERT_NOT_NULL(protocol6);
  ASSERT_EQ(protocol6->methods.size(), 1);
  EXPECT_EQ(protocol6->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol6->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidNormalMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasNormalMethod1 {
    MyMethod();
};

open protocol HasNormalMethod2 {
    MyMethod() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasNormalMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasNormalMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidStrictNormalMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasNormalMethod1 {
    strict MyMethod();
};

open protocol HasNormalMethod2 {
    strict MyMethod() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasNormalMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasNormalMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidFlexibleNormalMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

open protocol HasNormalMethod1 {
    flexible MyMethod();
};

open protocol HasNormalMethod2 {
    flexible MyMethod() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasNormalMethod1");
  ASSERT_NOT_NULL(protocol1);
  ASSERT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasNormalMethod2");
  ASSERT_NOT_NULL(protocol2);
  ASSERT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidEvent) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

protocol HasEvent {
    -> MyEvent();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasEvent");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidStrictEvent) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

protocol HasEvent {
    strict -> MyMethod();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasEvent");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kStrict);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidFlexibleEvent) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

protocol HasEvent {
    flexible -> MyMethod();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasEvent");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

TEST(MethodTests, GoodValidStrictnessModifiers) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

closed protocol Closed {
  strict StrictOneWay();
  strict StrictTwoWay() -> ();
  strict -> StrictEvent();
};

ajar protocol Ajar {
  strict StrictOneWay();
  flexible FlexibleOneWay();

  strict StrictTwoWay() -> ();

  strict -> StrictEvent();
  flexible -> FlexibleEvent();
};

open protocol Open {
  strict StrictOneWay();
  flexible FlexibleOneWay();

  strict StrictTwoWay() -> ();
  flexible FlexibleTwoWay() -> ();

  strict -> StrictEvent();
  flexible -> FlexibleEvent();
};
)FIDL",
                      experiment_flags);
  ASSERT_COMPILED(library);

  auto closed = library.LookupProtocol("Closed");
  ASSERT_NOT_NULL(closed);
  ASSERT_EQ(closed->methods.size(), 3);

  auto ajar = library.LookupProtocol("Ajar");
  ASSERT_NOT_NULL(ajar);
  ASSERT_EQ(ajar->methods.size(), 5);

  auto open = library.LookupProtocol("Open");
  ASSERT_NOT_NULL(open);
  ASSERT_EQ(open->methods.size(), 6);
}

TEST(MethodTests, BadInvalidStrictnessFlexibleEventInClosed) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

closed protocol Closed {
  flexible -> Event();
};
)FIDL",
                      experiment_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleOneWayMethodInClosedProtocol);
}

TEST(MethodTests, BadInvalidStrictnessFlexibleOneWayMethodInClosed) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

closed protocol Closed {
  flexible Method();
};
)FIDL",
                      experiment_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleOneWayMethodInClosedProtocol);
}

TEST(MethodTests, BadInvalidStrictnessFlexibleTwoWayMethodInClosed) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

closed protocol Closed {
  flexible Method() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleTwoWayMethodRequiresOpenProtocol);
}

TEST(MethodTests, BadInvalidStrictnessFlexibleTwoWayMethodInAjar) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(library example;

ajar protocol Ajar {
  flexible Method() -> ();
};
)FIDL",
                      experiment_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleTwoWayMethodRequiresOpenProtocol);
}

TEST(MethodTests, BadInvalidOpennessModifierOnMethod) {
  auto experiment_flags =
      fidl::ExperimentalFlags(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  TestLibrary library(R"FIDL(
library example;

protocol BadMethod {
    open Method();
};

)FIDL",
                      experiment_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, GoodValidComposeMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    compose();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasMethod");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadStrictComposeMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    strict compose();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadFlexibleComposeMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    flexible compose();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, GoodValidStrictMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    strict();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasMethod");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadStrictStrictMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    strict strict();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadFlexibleStrictMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    flexible strict();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, GoodValidFlexibleTwoWayMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    flexible();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasMethod");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadStrictFlexibleTwoWayMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    strict flexible();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadFlexibleFlexibleTwoWayMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    flexible flexible();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, GoodValidNormalMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    MyMethod();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasMethod");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadStrictNormalMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    strict MyMethod();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadFlexibleNormalMethodWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasMethod {
    flexible MyMethod();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, GoodValidEventWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasEvent {
    -> OnSomething();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("HasEvent");
  ASSERT_NOT_NULL(protocol);
  ASSERT_EQ(protocol->methods.size(), 1);
  EXPECT_EQ(protocol->methods[0].strictness, fidl::types::Strictness::kFlexible);
  EXPECT_EQ(protocol->all_methods.size(), 1);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadStrictEventWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasEvent {
    strict -> OnSomething();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(MethodTests, BadFlexibleEventWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;
protocol HasEvent {
    flexible -> OnSomething();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

}  // namespace
