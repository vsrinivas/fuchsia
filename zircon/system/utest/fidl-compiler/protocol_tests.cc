// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(ProtocolTests, GoodValidEmptyProtocol) {
  TestLibrary library(R"FIDL(
library example;

protocol Empty {};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NOT_NULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->all_methods.size(), 0);
}

TEST(ProtocolTests, GoodValidComposeMethod) {
  TestLibrary library(R"FIDL(
library example;

protocol HasComposeMethod1 {
    compose();
};

protocol HasComposeMethod2 {
    compose() -> ();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

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
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Parent {};
protocol Child : Parent {};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ProtocolTests, BadDocCommentOutsideAttributelist) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol WellDocumented {
    Method();
    /// Misplaced doc comment
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, GoodAttachAttributesToCompose) {
  TestLibrary library(R"FIDL(
library example;

protocol ParentA {
    ParentMethodA();
};

protocol ParentB {
    ParentMethodB();
};

protocol Child {
    [ThisIsAllowed] compose ParentA;
    /// This is also allowed.
    compose ParentB;
    ChildMethod();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto child_protocol = library.LookupProtocol("Child");
  ASSERT_NOT_NULL(child_protocol);
  EXPECT_EQ(child_protocol->methods.size(), 1);
  EXPECT_EQ(child_protocol->all_methods.size(), 3);
  ASSERT_EQ(child_protocol->composed_protocols.size(), 2);
  EXPECT_EQ(child_protocol->composed_protocols.front().attributes->attributes.size(), 1);
  EXPECT_EQ(child_protocol->composed_protocols.front().attributes->attributes.front()->name,
            "ThisIsAllowed");
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.size(), 1);
  EXPECT_EQ(child_protocol->composed_protocols.back().attributes->attributes.front()->name, "Doc");
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Narcisse {
    compose Narcisse;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(ProtocolTests, BadCannotComposeSameProtocolTwice) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Parent {
    Method();
};

protocol Child {
    compose Parent;
    compose Parent;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrProtocolComposedMultipleTimes);
}

TEST(ProtocolTests, BadCannotComposeMissingProtocol) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Child {
    compose MissingParent;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MissingParent");
}

TEST(ProtocolTests, BadCannotComposeNonProtocol) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {};
protocol P {
    compose S;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrComposingNonProtocol);
}

TEST(ProtocolTests, BadCannotUseOrdinalsInProtocolDeclaration) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol NoMoreOrdinals {
    42: NiceTry();
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedProtocolMember);
}

TEST(ProtocolTests, BadNoOtherPragmaThanCompose) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol Wrong {
    not_compose Something;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnrecognizedProtocolMember);
}

TEST(ProtocolTests, BadComposedProtocolsHaveClashingNames) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodName);
}

// See GetGeneratedOrdinal64ForTesting in test_library.h
// See GetGeneratedOrdinal64ForTesting in test_library.h
TEST(ProtocolTests, BadComposedProtocolsHaveClashingOrdinals) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library methodhasher;

protocol SpecialComposed {
   ClashOne();
};

protocol Special {
    compose SpecialComposed;
    ClashTwo();
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ClashTwo_");
}

TEST(ProtocolTests, BadSimpleConstraintAppliesToComposedMethodsToo) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMemberMustBeSimple);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "arg");
}

TEST(ProtocolTests, BadRequestMustBeProtocol) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  // TODO(fxbug.dev/75112): currently need to specify second constraint to get
  // the more specific error
  TestLibrary library(R"FIDL(
library example;

type S = struct {};
protocol P {
    Method(struct { r server_end:<S, optional>; });
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeAProtocol);
}

TEST(ProtocolTests, BadRequestMustBeParameterized) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {
    Method(struct { r server_end; });
};
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): more specific error in the new syntax since it goes
  // through a separate code path.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrProtocolConstraintRequired);
}

TEST(ProtocolTests, BadRequestCannotHaveSize) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {};
type S = struct {
    p server_end:<P,0>;
};
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): more general error in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(ProtocolTests, BadDuplicateParameterName) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {
  MethodWithDuplicateParams(struct {foo uint8; foo uint8; });
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMethodParameterName);
}

TEST(ProtocolTests, BadParameterizedTypedChannel) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

type Foo = resource struct {
  foo client_end<MyProtocol>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ProtocolTests, BadTooManyConstraintsTypedChannel) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

type Foo = resource struct {
  foo client_end:<MyProtocol, optional, foo, bar>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(ProtocolTests, GoodTypedChannels) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

resource struct Foo {
  MyProtocol a;
  MyProtocol? b;
  request<MyProtocol> c;
  request<MyProtocol>? d;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto container = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(container);
  ASSERT_EQ(container->members.size(), 4);

  size_t i = 0;

  auto a_type_base = GetType(container->members[i++].type_ctor);
  ASSERT_EQ(a_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  const auto* a_type = static_cast<const fidl::flat::IdentifierType*>(a_type_base);
  EXPECT_EQ(a_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(a_type->type_decl->kind, fidl::flat::Decl::Kind::kProtocol);

  auto b_type_base = GetType(container->members[i++].type_ctor);
  ASSERT_EQ(b_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  const auto* b_type = static_cast<const fidl::flat::IdentifierType*>(b_type_base);
  EXPECT_EQ(b_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(b_type->type_decl->kind, fidl::flat::Decl::Kind::kProtocol);

  auto c_type_base = GetType(container->members[i++].type_ctor);
  ASSERT_EQ(c_type_base->kind, fidl::flat::Type::Kind::kRequestHandle);
  const auto* c_type = static_cast<const fidl::flat::RequestHandleType*>(c_type_base);
  EXPECT_EQ(c_type->nullability, fidl::types::Nullability::kNonnullable);

  auto d_type_base = GetType(container->members[i++].type_ctor);
  ASSERT_EQ(d_type_base->kind, fidl::flat::Type::Kind::kRequestHandle);
  const auto* d_type = static_cast<const fidl::flat::RequestHandleType*>(d_type_base);
  EXPECT_EQ(d_type->nullability, fidl::types::Nullability::kNullable);
}

TEST(ProtocolTests, GoodPartialTypedChannelConstraints) {
  TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {};

alias ClientEnd = MyProtocol;
alias ServerEnd = request<MyProtocol>;

resource struct Foo {
  ClientEnd a;
  ClientEnd? b;
  ServerEnd c;
  ServerEnd? d;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

}  // namespace
