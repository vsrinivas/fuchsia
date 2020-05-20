// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool valid_empty_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Empty {};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto protocol = library.LookupProtocol("Empty");
  ASSERT_NONNULL(protocol);

  EXPECT_EQ(protocol->methods.size(), 0);
  EXPECT_EQ(protocol->all_methods.size(), 0);

  END_TEST;
}

bool valid_compose_method() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol HasComposeMethod1 {
    compose();
};

protocol HasComposeMethod2 {
    compose() -> ();
};

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto protocol1 = library.LookupProtocol("HasComposeMethod1");
  ASSERT_NONNULL(protocol1);
  EXPECT_EQ(protocol1->methods.size(), 1);
  EXPECT_EQ(protocol1->all_methods.size(), 1);

  auto protocol2 = library.LookupProtocol("HasComposeMethod2");
  ASSERT_NONNULL(protocol2);
  EXPECT_EQ(protocol2->methods.size(), 1);
  EXPECT_EQ(protocol2->all_methods.size(), 1);

  END_TEST;
}

bool valid_protocol_composition() {
  BEGIN_TEST;

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
  ASSERT_TRUE(library.Compile());

  auto protocol_a = library.LookupProtocol("A");
  ASSERT_NONNULL(protocol_a);
  EXPECT_EQ(protocol_a->methods.size(), 1);
  EXPECT_EQ(protocol_a->all_methods.size(), 1);

  auto protocol_b = library.LookupProtocol("B");
  ASSERT_NONNULL(protocol_b);
  EXPECT_EQ(protocol_b->methods.size(), 1);
  EXPECT_EQ(protocol_b->all_methods.size(), 2);

  auto protocol_c = library.LookupProtocol("C");
  ASSERT_NONNULL(protocol_c);
  EXPECT_EQ(protocol_c->methods.size(), 1);
  EXPECT_EQ(protocol_c->all_methods.size(), 2);

  auto protocol_d = library.LookupProtocol("D");
  ASSERT_NONNULL(protocol_d);
  EXPECT_EQ(protocol_d->methods.size(), 1);
  EXPECT_EQ(protocol_d->all_methods.size(), 4);

  END_TEST;
}

bool invalid_colon_syntax_is_not_supported() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Parent {};
protocol Child : Parent {};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);

  END_TEST;
}

bool invalid_doc_comment_outside_attribute_list() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol WellDocumented {
    Method();
    /// Misplaced doc comment
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrExpectedProtocolMember);

  END_TEST;
}

bool invalid_cannot_attach_attributes_to_compose() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Child {
    [NoCantDo] compose Parent;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotAttachAttributesToCompose);

  END_TEST;
}

bool invalid_cannot_compose_yourself() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Narcisse {
    compose Narcisse;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrIncludeCycle);

  END_TEST;
}

bool invalid_cannot_compose_twice_the_same_protocol() {
  BEGIN_TEST;

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
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrProtocolComposedMultipleTimes);

  END_TEST;
}

bool invalid_cannot_compose_missing_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Child {
    compose MissingParent;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnknownType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "MissingParent");

  END_TEST;
}

bool invalid_cannot_compose_non_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {};
protocol P {
    compose S;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrComposingNonProtocol);

  END_TEST;
}

bool invalid_cannot_use_ordinals_in_protocol_declaration() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol NoMoreOrdinals {
    42: NiceTry();
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrExpectedProtocolMember);

  END_TEST;
}

bool invalid_no_other_pragma_than_compose() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol Wrong {
    not_compose Something;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnrecognizedProtocolMember);

  END_TEST;
}

bool invalid_composed_protocols_have_clashing_names() {
  BEGIN_TEST;

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
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodName);

  END_TEST;
}

bool invalid_composed_protocols_have_clashing_ordinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library a;

// a.b/lo and a.cv/f have colliding computed ordinals, so this is an illegal
// FIDL definition.

protocol b {
   lo();
};

protocol cv {
    compose b;
    f();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodOrdinal);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "f_");

  END_TEST;
}

bool invalid_simple_constraint_applies_to_composed_methods_too() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol NotSimple {
    Complex(vector<uint64> arg);
};

[ForDeprecatedCBindings]
protocol YearningForSimplicity {
    compose NotSimple;
    Simple();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMemberMustBeSimple);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "arg");

  END_TEST;
}

bool invalid_request_must_be_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {};
protocol P {
    Method(request<S> r);
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustBeAProtocol);

  END_TEST;
}

bool invalid_request_must_be_parameterized() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol P {
    Method(request r);
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);

  END_TEST;
}

bool invalid_request_cannot_have_size() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol P {};
struct S {
    request<P>:0 p;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotHaveSize);

  END_TEST;
}

bool invalid_duplicate_parameter_name() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol P {
  MethodWithDuplicateParams(uint8 foo, uint8 foo);
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMethodParameterName);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(protocol_tests)
RUN_TEST(valid_empty_protocol)
RUN_TEST(valid_compose_method)
RUN_TEST(valid_protocol_composition)
RUN_TEST(invalid_colon_syntax_is_not_supported)
RUN_TEST(invalid_doc_comment_outside_attribute_list)
RUN_TEST(invalid_cannot_attach_attributes_to_compose)
RUN_TEST(invalid_cannot_compose_yourself)
RUN_TEST(invalid_cannot_compose_twice_the_same_protocol)
RUN_TEST(invalid_cannot_compose_missing_protocol)
RUN_TEST(invalid_cannot_compose_non_protocol)
RUN_TEST(invalid_cannot_use_ordinals_in_protocol_declaration)
RUN_TEST(invalid_no_other_pragma_than_compose)
RUN_TEST(invalid_composed_protocols_have_clashing_names)
RUN_TEST(invalid_composed_protocols_have_clashing_ordinals)
RUN_TEST(invalid_simple_constraint_applies_to_composed_methods_too)
RUN_TEST(invalid_request_must_be_protocol)
RUN_TEST(invalid_request_must_be_parameterized)
RUN_TEST(invalid_request_cannot_have_size)
RUN_TEST(invalid_duplicate_parameter_name)
END_TEST_CASE(protocol_tests)
