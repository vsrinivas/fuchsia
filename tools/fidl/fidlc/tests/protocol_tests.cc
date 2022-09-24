// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat/types.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(ProtocolTests, GoodValidEmptyProtocol) {
  TestLibrary library(R"FIDL(library example;

protocol Empty {};
)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->openness, fidl::types::Openness::kOpen);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

TEST(ProtocolTests, GoodValidEmptyOpenProtocol) {
  TestLibrary library(R"FIDL(library example;

open protocol Empty {};
)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->openness, fidl::types::Openness::kOpen);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

TEST(ProtocolTests, GoodValidEmptyAjarProtocol) {
  TestLibrary library(R"FIDL(library example;

ajar protocol Empty {};
)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->openness, fidl::types::Openness::kAjar);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

TEST(ProtocolTests, GoodValidEmptyClosedProtocol) {
  TestLibrary library(R"FIDL(library example;

closed protocol Empty {};
)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->openness, fidl::types::Openness::kClosed);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, GoodValidEmptyProtocolWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(library example;

protocol Empty {};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->openness, fidl::types::Openness::kOpen);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadOpenProtocolWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(
library example;

open protocol Empty {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadAjarProtocolWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(
library example;

ajar protocol Empty {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadClosedProtocolWithoutUnknownInteractions) {
  TestLibrary library(R"FIDL(
library example;

closed protocol Empty {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ProtocolTests, BadEmptyStrictProtocol) {
  TestLibrary library(R"FIDL(
library example;

strict protocol Empty {};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ProtocolTests, BadEmptyFlexibleProtocol) {
  TestLibrary library(R"FIDL(
library example;

flexible protocol Empty {};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedDeclaration);
}

TEST(ProtocolTests, BadOpenMissingProtocolToken) {
  TestLibrary library(R"FIDL(
library example;

open Empty {};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ProtocolTests, BadAjarMissingProtocolToken) {
  TestLibrary library(R"FIDL(
library example;

ajar Empty {};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ProtocolTests, BadClosedMissingProtocolToken) {
  TestLibrary library(R"FIDL(
library example;

closed Empty {};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedIdentifier);
}

TEST(ProtocolTests, BadEmptyProtocolMember) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  ;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, GoodValidProtocolComposition) {
  TestLibrary library(R"FIDL(library example;

protocol A {
    MethodA();
};

protocol B {
    compose A;
    MethodB();
};

protocol C {
    compose A;
    MethodC();
};

protocol D {
    compose B;
    compose C;
    MethodD();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol_a = library.LookupProtocol("A");
  ASSERT_NOT_NULL(protocol_a);
  EXPECT_EQ(protocol_a->methods.size(), 1);
  EXPECT_EQ(protocol_a->all_methods.size(), 1);

  auto protocol_b = library.LookupProtocol("B");
  ASSERT_NOT_NULL(protocol_b);
  EXPECT_EQ(protocol_b->methods.size(), 1);
  EXPECT_EQ(protocol_b->all_methods.size(), 2);

  auto protocol_c = library.LookupProtocol("C");
  ASSERT_NOT_NULL(protocol_c);
  EXPECT_EQ(protocol_c->methods.size(), 1);
  EXPECT_EQ(protocol_c->all_methods.size(), 2);

  auto protocol_d = library.LookupProtocol("D");
  ASSERT_NOT_NULL(protocol_d);
  EXPECT_EQ(protocol_d->methods.size(), 1);
  EXPECT_EQ(protocol_d->all_methods.size(), 4);
}

TEST(ProtocolTests, GoodValidOpenClosedProtocolComposition) {
  TestLibrary library(R"FIDL(
library example;

closed protocol Closed {};
ajar protocol Ajar {};
open protocol Open {};

closed protocol ComposeInClosed {
  compose Closed;
};

ajar protocol ComposeInAjar {
  compose Closed;
  compose Ajar;
};

open protocol ComposeInOpen {
  compose Closed;
  compose Ajar;
  compose Open;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_COMPILED(library);

  auto compose_in_closed = library.LookupProtocol("ComposeInClosed");
  ASSERT_NOT_NULL(compose_in_closed);
  EXPECT_EQ(compose_in_closed->composed_protocols.size(), 1);

  auto compose_in_ajar = library.LookupProtocol("ComposeInAjar");
  ASSERT_NOT_NULL(compose_in_ajar);
  EXPECT_EQ(compose_in_ajar->composed_protocols.size(), 2);

  auto compose_in_open = library.LookupProtocol("ComposeInOpen");
  ASSERT_NOT_NULL(compose_in_open);
  EXPECT_EQ(compose_in_open->composed_protocols.size(), 3);
}

TEST(ProtocolTests, BadInvalidComposeOpenInClosed) {
  TestLibrary library(R"FIDL(
library example;

open protocol Composed {};

closed protocol Composing {
  compose Composed;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrComposedProtocolTooOpen);
}

TEST(ProtocolTests, BadInvalidComposeAjarInClosed) {
  TestLibrary library(R"FIDL(
library example;

ajar protocol Composed {};

closed protocol Composing {
  compose Composed;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrComposedProtocolTooOpen);
}

TEST(ProtocolTests, BadInvalidComposeOpenInAjar) {
  TestLibrary library(R"FIDL(
library example;

open protocol Composed {};

ajar protocol Composing {
  compose Composed;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrComposedProtocolTooOpen);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadModifierStrictOnComposeWithoutUnkownInteractions) {
  TestLibrary library(R"FIDL(
library example;

protocol A {};

protocol B {
  strict compose A;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadModifierFlexibleOnComposeWithoutUnkownInteractions) {
  TestLibrary library(R"FIDL(
library example;

protocol A {};

protocol B {
  flexible compose A;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadModifierStrictOnInvalidMemberWithoutUnkownInteractions) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  strict;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

// TODO(fxb/88366): remove checks for behavior with unknown interactions turned
// off when unknown interactions are always-on.
TEST(ProtocolTests, BadModifierFlexibleOnInvalidMemberWithoutUnkownInteractions) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  flexible;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadModifierStrictOnCompose) {
  TestLibrary library(R"FIDL(
library example;

protocol A {};

protocol B {
  strict compose A;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadModifierFlexibleOnCompose) {
  TestLibrary library(R"FIDL(
library example;

protocol A {};

protocol B {
  flexible compose A;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadModifierStrictOnInvalidMember) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  strict;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, BadModifierFlexibleOnInvalidMember) {
  TestLibrary library(R"FIDL(
library example;

protocol Example {
  flexible;
};

)FIDL");
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kUnknownInteractions);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, BadColonNotSupported) {
  TestLibrary library(R"FIDL(
library example;

protocol Parent {};
protocol Child : Parent {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ProtocolTests, BadDocCommentOutsideAttributelist) {
  TestLibrary library(R"FIDL(
library example;

protocol WellDocumented {
    Method();
    /// Misplaced doc comment
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, GoodAttachAttributesToCompose) {
  TestLibrary library(R"FIDL(library example;

protocol ParentA {
    ParentMethodA();
};

protocol ParentB {
    ParentMethodB();
};

protocol Child {
    @this_is_allowed
    compose ParentA;
    /// This is also allowed.
    compose ParentB;
    ChildMethod();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto child_protocol = library.LookupProtocol("Child");
  ASSERT_NOT_NULL(child_protocol);
  EXPECT_EQ(child_protocol->methods.size(), 1);
  EXPECT_EQ(child_protocol->all_methods.size(), 3);
  ASSERT_EQ(child_protocol->composed_protocols.size(), 2);
  EXPECT_EQ(child_protocol->composed_protocols.front().attributes->attributes.size(), 1);
  EXPECT_EQ(child_protocol->composed_protocols.front().attributes->attributes.front()->name.data(),
            "this_is_allowed");
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.size(), 1);
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->name.data(),
            "doc");
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->span.data(),
            "/// This is also allowed.");
  ASSERT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->args.size(),
            1);
  EXPECT_TRUE(child_protocol->composed_protocols.back()
                  .attributes->attributes.front()
                  ->args.front()
                  ->value->IsResolved());
}

TEST(ProtocolTests, BadCannotComposeYourself) {
  TestLibrary library(R"FIDL(
library example;

protocol Narcisse {
    compose Narcisse;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "protocol 'Narcisse' -> protocol 'Narcisse'");
}

TEST(ProtocolTests, BadCannotMutuallyCompose) {
  TestLibrary library;
  library.AddFile("bad/recursive_protocol_reference.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(),
                "protocol 'Yang' -> protocol 'Yin' -> protocol 'Yang'");
}

TEST(ProtocolTests, BadCannotComposeSameProtocolTwice) {
  TestLibrary library(R"FIDL(
library example;

protocol Parent {
    Method();
};

protocol Child {
    compose Parent;
    compose Parent;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrProtocolComposedMultipleTimes);
}

TEST(ProtocolTests, BadCannotComposeMissingProtocol) {
  TestLibrary library(R"FIDL(
library example;

protocol Child {
    compose MissingParent;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MissingParent");
}

TEST(ProtocolTests, BadCannotComposeNonProtocol) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {};
protocol P {
    compose S;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrComposingNonProtocol);
}

TEST(ProtocolTests, BadCannotUseOrdinalsInProtocolDeclaration) {
  TestLibrary library(R"FIDL(
library example;

protocol NoMoreOrdinals {
    42: NiceTry();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, BadEmptyNamedItem) {
  TestLibrary library(R"FIDL(
library example;

protocol NoMoreOrdinals {
    NotActuallyAMethod;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadNoOtherPragmaThanCompose) {
  TestLibrary library(R"FIDL(
library example;

protocol Wrong {
    not_compose Something;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadComposedProtocolsHaveClashingNames) {
  TestLibrary library(R"FIDL(
library example;

protocol A {
    MethodA();
};

protocol B {
    compose A;
    MethodB();
};

protocol C {
    compose A;
    MethodC();
};

protocol D {
    compose B;
    compose C;
    MethodD();
    MethodA();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodName);
}

// See GetGeneratedOrdinal64ForTesting in test_library.h
// See GetGeneratedOrdinal64ForTesting in test_library.h
TEST(ProtocolTests, BadComposedProtocolsHaveClashingOrdinals) {
  TestLibrary library(R"FIDL(
library methodhasher;

protocol SpecialComposed {
   ClashOne();
};

protocol Special {
    compose SpecialComposed;
    ClashTwo();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ClashTwo_");
}

TEST(ProtocolTests, BadSimpleConstraintAppliesToComposedMethodsToo) {
  TestLibrary library(R"FIDL(
library example;

protocol NotSimple {
    Complex(struct { arg vector<uint64>; });
};

@for_deprecated_c_bindings
protocol YearningForSimplicity {
    compose NotSimple;
    Simple();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMemberMustBeSimple);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "arg");
  EXPECT_SUBSTR(library.errors()[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(ProtocolTests, BadRequestMustBeProtocol) {
  // TODO(fxbug.dev/75112): currently need to specify second constraint to get
  // the more specific error
  TestLibrary library(R"FIDL(
library example;

type S = struct {};
protocol P {
    Method(struct { r server_end:<S, optional>; });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeAProtocol);
}

TEST(ProtocolTests, BadRequestMustBeParameterized) {
  TestLibrary library(R"FIDL(
library example;

protocol P {
    Method(struct { r server_end; });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrProtocolConstraintRequired);
  EXPECT_EQ(library.errors()[0]->span.data(), "server_end");
}

TEST(ProtocolTests, BadRequestCannotHaveSize) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
type S = struct {
    p server_end:<P,0>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(ProtocolTests, BadDuplicateParameterName) {
  TestLibrary library(R"FIDL(
library example;

protocol P {
  MethodWithDuplicateParams(struct {foo uint8; foo uint8; });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
}

TEST(ProtocolTests, BadParameterizedTypedChannel) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

type Foo = resource struct {
  foo client_end<MyProtocol>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ProtocolTests, BadTooManyConstraintsTypedChannel) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

type Foo = resource struct {
  foo client_end:<MyProtocol, optional, 1, 2>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(ProtocolTests, GoodTypedChannels) {
  TestLibrary library(R"FIDL(library example;

protocol MyProtocol {};

type Foo = resource struct {
    a client_end:MyProtocol;
    b client_end:<MyProtocol, optional>;
    c server_end:MyProtocol;
    d server_end:<MyProtocol, optional>;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto container = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(container);
  ASSERT_EQ(container->members.size(), 4);

  size_t i = 0;

  auto a_type_base = container->members[i++].type_ctor->type;
  ASSERT_EQ(a_type_base->kind, fidl::flat::Type::Kind::kTransportSide);
  const auto* a_type = static_cast<const fidl::flat::TransportSideType*>(a_type_base);
  EXPECT_EQ(a_type->end, fidl::flat::TransportSide::kClient);
  EXPECT_EQ(a_type->nullability, fidl::types::Nullability::kNonnullable);

  auto b_type_base = container->members[i++].type_ctor->type;
  ASSERT_EQ(b_type_base->kind, fidl::flat::Type::Kind::kTransportSide);
  const auto* b_type = static_cast<const fidl::flat::TransportSideType*>(b_type_base);
  EXPECT_EQ(b_type->end, fidl::flat::TransportSide::kClient);
  EXPECT_EQ(b_type->nullability, fidl::types::Nullability::kNullable);

  auto c_type_base = container->members[i++].type_ctor->type;
  ASSERT_EQ(c_type_base->kind, fidl::flat::Type::Kind::kTransportSide);
  const auto* c_type = static_cast<const fidl::flat::TransportSideType*>(c_type_base);
  EXPECT_EQ(c_type->end, fidl::flat::TransportSide::kServer);
  EXPECT_EQ(c_type->nullability, fidl::types::Nullability::kNonnullable);

  auto d_type_base = container->members[i++].type_ctor->type;
  ASSERT_EQ(d_type_base->kind, fidl::flat::Type::Kind::kTransportSide);
  const auto* d_type = static_cast<const fidl::flat::TransportSideType*>(d_type_base);
  EXPECT_EQ(d_type->end, fidl::flat::TransportSide::kServer);
  EXPECT_EQ(d_type->nullability, fidl::types::Nullability::kNullable);
}

TEST(ProtocolTests, GoodPartialTypedChannelConstraints) {
  TestLibrary library(R"FIDL(library example;

protocol MyProtocol {};

alias ClientEnd = client_end:MyProtocol;
alias ServerEnd = server_end:MyProtocol;

type Foo = resource struct {
    a ClientEnd;
    b ClientEnd:optional;
    c ServerEnd;
    d ServerEnd:optional;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodStructSimpleLayout) {
  TestLibrary library(R"FIDL(
library example;

@for_deprecated_c_bindings
protocol MyProtocol {
  -> OnMyEvent(struct {
    b bool;
  });
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, BadMethodStructSimpleLayout) {
  TestLibrary library(R"FIDL(
library example;

@for_deprecated_c_bindings
protocol MyProtocol {
  -> OnMyEvent(struct {
    b vector<bool>;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMemberMustBeSimple);
  EXPECT_SUBSTR(library.errors()[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(ProtocolTests, BadMethodStructSizeConstraints) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyStruct = resource struct {
  a client_end:<MyProtocol>;
};

@max_handles("0") @max_bytes("1")
protocol MyProtocol {
  MyMethod(MyStruct) -> (MyStruct) error uint32;
  -> OnMyEvent(struct { b uint16; });
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  // Both uses of "MyStruct" use too many handles.
  EXPECT_ERR(library.errors()[0], fidl::ErrTooManyHandles);
  EXPECT_ERR(library.errors()[1], fidl::ErrTooManyHandles);

  // Both uses of "MyStruct," as well as the anonymous layout, use too many bytes.
  EXPECT_ERR(library.errors()[2], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[3], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[4], fidl::ErrTooManyBytes);
}

TEST(ProtocolTests, BadMethodStructLayoutDefaultMember) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(struct {
    @allow_deprecated_struct_defaults
    foo uint8 = 1;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrPayloadStructHasDefaultMembers);
}

TEST(ProtocolTests, BadMethodEmptyPayloadStruct) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(struct {}) -> (struct {});
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrEmptyPayloadStructs,
                                      fidl::ErrEmptyPayloadStructs);
}

TEST(ProtocolTests, BadMethodEnumLayout) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod(enum {
    FOO = 1;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListKind);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "enum");
}

TEST(ProtocolTests, BadMethodEmptyResponseWithError) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  MyMethod() -> () error uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrResponsesWithErrorsMustNotBeEmpty);
}

TEST(ProtocolTests, GoodMethodNamedTypeRequest) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{
  a bool;
};

protocol MyProtocol {
    MyMethodOneWay(MyStruct);
    MyMethodTwoWay(MyStruct) -> ();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodNamedTypeResponse) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{
  a bool;
};

protocol MyProtocol {
  MyMethod() -> (MyStruct);
    -> OnMyEvent(MyStruct);
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodNamedTypeResultPayload) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{
  a bool;
};

protocol MyProtocol {
  MyMethod() -> (MyStruct) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodNamedAlias) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct {
  a bool;
};

alias MyStructAlias = MyStruct;
alias MyAliasAlias = MyStructAlias;

protocol MyProtocol {
    MyMethod(MyStructAlias) -> (MyAliasAlias);
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, BadMethodNamedEmptyPayloadStruct) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{};

protocol MyProtocol {
    MyMethod(MyStruct) -> (MyStruct);
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrEmptyPayloadStructs,
                                      fidl::ErrEmptyPayloadStructs);
}

TEST(ProtocolTests, BadMethodNamedDefaultValueStruct) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct{
  @allow_deprecated_struct_defaults
  a bool = false;
};

protocol MyProtocol {
    MyMethod(MyStruct) -> (MyStruct);
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrPayloadStructHasDefaultMembers,
                                      fidl::ErrPayloadStructHasDefaultMembers);
}

TEST(ProtocolTests, BadMethodNamedInvalidHandle) {
  TestLibrary library(R"FIDL(
library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

type rights = strict bits : uint32 {
    TRANSFER = 1;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};

protocol MyProtocol {
    MyMethod(handle);
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "handle");
}

TEST(ProtocolTests, BadMethodNamedInvalidAlias) {
  TestLibrary library(R"FIDL(
library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

type rights = strict bits : uint32 {
    TRANSFER = 1;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};

alias MyPrimAlias = bool;
alias MyHandleAlias = handle;
alias MyVectorAlias = vector<MyPrimAlias>;
alias MyAliasAlias = MyVectorAlias:optional;

protocol MyProtocol {
    MyMethod(MyPrimAlias) -> (MyHandleAlias);
    MyOtherMethod(MyVectorAlias) -> (MyAliasAlias);
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  ASSERT_ERR(library.errors()[0], fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "bool");
  ASSERT_ERR(library.errors()[1], fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[1]->msg.c_str(), "example/handle");

  ASSERT_ERR(library.errors()[2], fidl::ErrInvalidParameterListType);
  ASSERT_SUBSTR(library.errors()[2]->msg.c_str(), "vector<bool>");
  ASSERT_ERR(library.errors()[3], fidl::ErrInvalidParameterListType);
  // TODO(fxbug.dev/93999): Should be "vector<bool>:optional".
  ASSERT_SUBSTR(library.errors()[3]->msg.c_str(), "vector<bool>?");
}

TEST(ProtocolTests, BadMethodNamedInvalidKind) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {
  MyOtherMethod();
};

service MyService {
  my_other_protocol client_end:MyOtherProtocol;
};

protocol MyProtocol {
    MyMethod(MyOtherProtocol) -> (MyService);
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrExpectedType, fidl::ErrExpectedType);
}

TEST(ProtocolTests, BadMethodTableSizeConstraints) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyTable = resource table {
  1: a client_end:<MyProtocol>;
};

@max_handles("0") @max_bytes("1")
protocol MyProtocol {
  MyMethod(MyTable) -> (MyTable) error uint32;
  -> OnMyEvent(table {
    1: b bool;
  });
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  // Both uses of "MyTable" use too many handles.
  EXPECT_ERR(library.errors()[0], fidl::ErrTooManyHandles);
  EXPECT_ERR(library.errors()[1], fidl::ErrTooManyHandles);

  // Both uses of "MyTable," as well as the anonymous layout, use too many bytes.
  EXPECT_ERR(library.errors()[2], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[3], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[4], fidl::ErrTooManyBytes);
}

TEST(ProtocolTests, BadMethodTableSimpleLayout) {
  TestLibrary library(R"FIDL(
library example;

@for_deprecated_c_bindings
protocol MyProtocol {
  -> OnMyEvent(table {
    1: b bool;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTableCannotBeSimple);
  EXPECT_SUBSTR(library.errors()[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(ProtocolTests, GoodMethodTableRequest) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyTable = resource table {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethodOneWay(table {
    1: b bool;
  });
  MyMethodTwoWay(MyTable) -> ();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodTableResponse) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyTable = resource table {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethod() -> (table {
    1: b bool;
  });
  -> OnMyEvent(MyTable);
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodTableResultPayload) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyTable = resource table {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethod() -> (MyTable) error uint32;
  MyAnonResponseMethod() -> (table {
    1: b bool;
  }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodUnionRequest) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyUnion = strict resource union {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethodOneWay(flexible union {
    1: b bool;
  });
  MyMethodTwoWay(MyUnion) -> ();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodUnionResponse) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyUnion = strict resource union {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethod() -> (flexible union {
    1: b bool;
  });
  -> OnMyEvent(MyUnion);
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, GoodMethodUnionResultPayload) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyUnion = strict resource union {
  1: a client_end:<MyProtocol>;
};

protocol MyProtocol {
  MyMethod() -> (MyUnion) error uint32;
  MyAnonResponseMethod() -> (flexible union {
    1: b bool;
  }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ProtocolTests, BadMethodUnionSizeConstraints) {
  TestLibrary library(R"FIDL(
library example;

protocol MyOtherProtocol {};

type MyUnion = strict resource union {
  1: a client_end:<MyProtocol>;
};

@max_handles("0") @max_bytes("1")
protocol MyProtocol {
  MyMethod(MyUnion) -> (MyUnion) error uint32;
  -> OnMyEvent(flexible union { 1: b bool; });
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  // Both uses of "MyUnion" use too many handles.
  EXPECT_ERR(library.errors()[0], fidl::ErrTooManyHandles);
  EXPECT_ERR(library.errors()[1], fidl::ErrTooManyHandles);

  // Both uses of "MyUnion," as well as the anonymous layout, use too many bytes.
  EXPECT_ERR(library.errors()[2], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[3], fidl::ErrTooManyBytes);
  EXPECT_ERR(library.errors()[4], fidl::ErrTooManyBytes);
}

TEST(ProtocolTests, BadMethodUnionSimpleLayout) {
  TestLibrary library(R"FIDL(
library example;

@for_deprecated_c_bindings
protocol MyProtocol {
  -> OnMyEvent(flexible union {
    1: b bool;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnionCannotBeSimple);
  EXPECT_SUBSTR(library.errors()[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(ProtocolTests, BadEventErrorSyntax) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
  -> OnMyEvent(struct {}) error int32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEventErrorSyntaxDeprecated);
}

TEST(ProtocolTests, BadDisallowedRequestType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod(uint32);
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
}

TEST(ProtocolTests, BadInvalidRequestType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod(box);
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ProtocolTests, BadDisallowedResponseType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod() -> (uint32);
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
}

TEST(ProtocolTests, BadInvalidResponseType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod() -> (box);
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ProtocolTests, BadDisallowedSuccessType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod() -> (uint32) error uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidParameterListType);
}

TEST(ProtocolTests, BadInvalidSuccessType) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod() -> (box) error uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

// TODO(fxbug.dev/93542): add bad `:optional` message body tests here.

}  // namespace
