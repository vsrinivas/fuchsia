// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

TEST(ProtocolTests, GoodValidEmptyProtocol) {
  TestLibrary library(R"FIDL(library example;

protocol Empty {};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

TEST(ProtocolTests, GoodValidComposeMethod) {
  TestLibrary library(R"FIDL(library example;

protocol HasComposeMethod1 {
    compose();
};

protocol HasComposeMethod2 {
    compose() -> ();
};
)FIDL");
  ASSERT_COMPILED(library);

  auto protocol1 = library.LookupProtocol("HasComposeMethod1");
  ASSERT_NOT_NULL(protocol1);
  EXPECT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasComposeMethod2");
  ASSERT_NOT_NULL(protocol2);
  EXPECT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->all_methods.size(), 1);
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
  EXPECT_EQ(child_protocol->composed_protocols.front().attributes->attributes.front()->name,
            "this_is_allowed");
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.size(), 1);
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->name, "doc");
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->span().data(),
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
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
  // NOTE(fxbug.dev/72924): more specific error in the new syntax since it goes
  // through a separate code path.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrProtocolConstraintRequired);
}

TEST(ProtocolTests, BadRequestCannotHaveSize) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
type S = struct {
    p server_end:<P,0>;
};
)FIDL");
  // NOTE(fxbug.dev/72924): more general error in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(ProtocolTests, BadDuplicateParameterName) {
  TestLibrary library(R"FIDL(
library example;

protocol P {
  MethodWithDuplicateParams(struct {foo uint8; foo uint8; });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodParameterName);
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
  foo client_end:<MyProtocol, optional, foo, bar>;
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

}  // namespace
