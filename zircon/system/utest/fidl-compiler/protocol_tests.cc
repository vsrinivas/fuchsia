// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

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

[FragileBase]
protocol A {
    MethodA();
};

[FragileBase]
protocol B {
    compose A;
    MethodB();
};

[FragileBase]
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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "unexpected token Colon, was expecting LeftCurly");

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "expected protocol member");

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Cannot attach attributes to compose stanza");

  END_TEST;
}

bool invalid_cannot_compose_yourself() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

[FragileBase]
protocol Narcisse {
    compose Narcisse;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "There is an includes-cycle in declaration");

  END_TEST;
}

bool invalid_cannot_compose_twice_the_same_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

[FragileBase]
protocol Parent {
    Method();
};

protocol Child {
    compose Parent;
    compose Parent;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "protocol composed multiple times");

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "unknown type MissingParent");

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "expected protocol member");

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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "unrecognized protocol member");

  END_TEST;
}

bool invalid_composed_protocols_have_clashing_names() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

[FragileBase]
protocol A {
    MethodA();
};

[FragileBase]
protocol B {
    compose A;
    MethodB();
};

[FragileBase]
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
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Multiple methods with the same name in a protocol");

  END_TEST;
}

bool invalid_composed_protocols_have_clashing_ordinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library a;

// a.b/lo and a.cv/f have colliding computed ordinals, so this is an illegal
// FIDL definition.

[FragileBase]
protocol b {
   lo();
};

[FragileBase]
protocol cv {
    compose b;
    f();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(),
                 "Multiple methods with the same ordinal in a protocol; "
                 "previous was at example.fidl:9:4. "
                 "Consider using attribute [Selector=\"f_\"] to change the name used to "
                 "calculate the ordinal.");

  END_TEST;
}

bool invalid_simple_constraint_applies_to_composed_methods_too() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

[FragileBase]
protocol NotSimple {
    Complex(vector<uint64> arg);
};

[Layout="Simple"]
protocol YearningForSimplicity {
    compose NotSimple;
    Simple();
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "member 'arg' is not simple");

  END_TEST;
}

bool invalid_missing_fragile_base_on_composed_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol NoFragileBase {
};

protocol Child {
    compose NoFragileBase;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(),
                 "protocol example/NoFragileBase is not marked by [FragileBase] "
                 "attribute, disallowing protocol example/Child from "
                 "composing it");

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
RUN_TEST(invalid_cannot_use_ordinals_in_protocol_declaration)
RUN_TEST(invalid_no_other_pragma_than_compose)
RUN_TEST(invalid_composed_protocols_have_clashing_names)
RUN_TEST(invalid_composed_protocols_have_clashing_ordinals)
RUN_TEST(invalid_simple_constraint_applies_to_composed_methods_too)
RUN_TEST(invalid_missing_fragile_base_on_composed_protocol)
END_TEST_CASE(protocol_tests)
