// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/index_walker2.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/symbols/index2.h"

namespace zxdb {

namespace {

bool EqualsElements(const IndexWalker2::Stage& a, const IndexWalker2::Stage& b) {
  if (a.size() != b.size())
    return false;

  for (const auto& cur : a) {
    if (std::find(b.begin(), b.end(), cur) == b.end())
      return false;
  }
  return true;
}

}  // namespace

TEST(IndexWalker2, ComponentMatchesNameOnly) {
  ParsedIdentifierComponent foo_comp("Foo");
  ParsedIdentifierComponent foo_template_comp("Foo", {"A", "b"});

  // Simple name-only comparisons.
  EXPECT_TRUE(IndexWalker2::ComponentMatchesNameOnly("Foo", foo_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatchesNameOnly("FooBar", foo_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatchesNameOnly("Fo2", foo_comp));

  // Component has a template, the index string doesn't.
  EXPECT_TRUE(IndexWalker2::ComponentMatchesNameOnly("Foo", foo_template_comp));

  // Component has no template, the index does (this input is non-canonical).
  EXPECT_TRUE(IndexWalker2::ComponentMatchesNameOnly("Foo < C >", foo_template_comp));
}

TEST(IndexWalker2, ComponentMatchesTemplateOnly) {
  ParsedIdentifierComponent foo_comp("Foo");
  ParsedIdentifierComponent foo_template_comp("Foo", {"A", "b"});
  ParsedIdentifierComponent foo_empty_template_comp("Foo", {});

  // Neither inputs have templates (should be a match).
  EXPECT_TRUE(IndexWalker2::ComponentMatchesTemplateOnly("Foo", foo_comp));

  // Template match but with different whitespace.
  EXPECT_TRUE(IndexWalker2::ComponentMatchesTemplateOnly("Foo < A,  b > ", foo_template_comp));

  // One has a template but the other doesn't.
  EXPECT_FALSE(IndexWalker2::ComponentMatchesTemplateOnly("Foo", foo_template_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatchesTemplateOnly("Foo<C>", foo_comp));

  // Empty template doesn't match no template.
  EXPECT_FALSE(IndexWalker2::ComponentMatchesTemplateOnly("Foo<>", foo_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatchesTemplateOnly("Foo", foo_empty_template_comp));
}

// Most cases are tested by ComponentMatchesNameOnly and ...TemplateOnly above.
TEST(IndexWalker2, ComponentMatches) {
  ParsedIdentifierComponent foo_comp("Foo");
  ParsedIdentifierComponent foo_template_comp("Foo", {"A", "b"});

  EXPECT_TRUE(IndexWalker2::ComponentMatches("Foo", foo_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatches("Foo<>", foo_comp));
  EXPECT_FALSE(IndexWalker2::ComponentMatches("Foo<>", foo_template_comp));
  EXPECT_TRUE(IndexWalker2::ComponentMatches("Foo <A,b >", foo_template_comp));
}

TEST(IndexWalker2, IsIndexStringBeyondName) {
  // Identity comparison.
  EXPECT_FALSE(IndexWalker2::IsIndexStringBeyondName("Foo", "Foo"));

  // Index nodes clearly before.
  EXPECT_FALSE(IndexWalker2::IsIndexStringBeyondName("Fo", "Foo"));
  EXPECT_FALSE(IndexWalker2::IsIndexStringBeyondName("Foa", "Foo"));

  // Index nodes clearly after.
  EXPECT_TRUE(IndexWalker2::IsIndexStringBeyondName("FooBar", "Foo"));
  EXPECT_TRUE(IndexWalker2::IsIndexStringBeyondName("Foz", "Foo"));
  EXPECT_TRUE(IndexWalker2::IsIndexStringBeyondName("Fz", "Foo"));

  // Templates in the index could has "not beyond".
  EXPECT_FALSE(IndexWalker2::IsIndexStringBeyondName("Foo<a>", "Foo"));
}

TEST(IndexWalker2, WalkInto) {
  Index2 index;
  auto& root = index.root();
  auto foo_node = root.AddChild(IndexNode2::Kind::kType, "Foo");
  root.AddChild(IndexNode2::Kind::kType, "Foo<Bar>");

  // These template names are non-canonical so we can verify the correct
  // comparisons happen.
  foo_node->AddChild(IndexNode2::Kind::kType, "Bar< int >");
  auto bar_int_char_node = foo_node->AddChild(IndexNode2::Kind::kType, "Bar< int,char >");

  // There could also be a non-template somewhere with the same name.
  auto bar_node = foo_node->AddChild(IndexNode2::Kind::kType, "Bar");

  // These nodes start with the prefix "Bar" for when we're searching. We test
  // things that will compare before and after "Bar<" ('9' before, 'f' after).
  auto barf_node = foo_node->AddChild(IndexNode2::Kind::kType, "Barf<int>");
  auto bar9_node = foo_node->AddChild(IndexNode2::Kind::kType, "Bar9<int>");

  IndexWalker2 walker(&index);
  EXPECT_TRUE(EqualsElements(walker.current(), {&root}));

  // Walking up at this point should be a no-op.
  EXPECT_FALSE(walker.WalkUp());
  EXPECT_TRUE(EqualsElements(walker.current(), {&root}));

  // Walk to the "Foo" component.
  EXPECT_TRUE(walker.WalkInto(ParsedIdentifierComponent("Foo")));
  EXPECT_TRUE(EqualsElements(walker.current(), {foo_node}));

  // Walk to the "NotPresent" component. The current location should be
  // unchanged.
  EXPECT_FALSE(walker.WalkInto(ParsedIdentifierComponent("NotFound")));
  EXPECT_TRUE(EqualsElements(walker.current(), {foo_node}));

  // Walk to the "Bar<int,char>" identifier.
  ParsedIdentifier bar_int_char;
  Err err = ExprParser::ParseIdentifier("Bar < int , char >", &bar_int_char);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_TRUE(walker.WalkInto(bar_int_char));
  EXPECT_TRUE(EqualsElements(walker.current(), {bar_int_char_node}));

  // Walk back up to "Foo".
  EXPECT_TRUE(walker.WalkUp());
  EXPECT_TRUE(EqualsElements(walker.current(), {foo_node}));

  // Walk to the "Bar" node.
  EXPECT_TRUE(walker.WalkInto(ParsedIdentifierComponent("Bar")));
  EXPECT_TRUE(EqualsElements(walker.current(), {bar_node}));

  // Parse the Barf identifier for the following two tests. This one has a
  // toplevel scope.
  ParsedIdentifier barf;
  err = ExprParser::ParseIdentifier("::Foo::Barf<int>", &barf);
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Walk to the "Foo::Bar9<int>" with copying the walker.
  {
    IndexWalker2 nested_walker(walker);
    ParsedIdentifier bar9;
    err = ExprParser::ParseIdentifier(":: Foo :: Bar9 < int >", &bar9);
    EXPECT_FALSE(err.has_error()) << err.msg();
    EXPECT_TRUE(nested_walker.WalkInto(bar9));
    EXPECT_TRUE(EqualsElements(nested_walker.current(), {bar9_node}));
  }

  // Walking from the root into the barf template should work.
  EXPECT_TRUE(walker.WalkInto(barf));
  EXPECT_TRUE(EqualsElements(walker.current(), {barf_node}));
}

// Tests that we can walk into multiple nodes of different types (namespaces, functions, etc.) at
// the same time when they have the same name.
TEST(IndexWalker2, WalkIntoMultiple) {
  Index2 index;
  auto& root = index.root();
  const char kFoo[] = "Foo";
  auto foo_type_node = root.AddChild(IndexNode2::Kind::kType, kFoo);
  auto foo_ns_node = root.AddChild(IndexNode2::Kind::kNamespace, kFoo);
  auto foo_func_node = root.AddChild(IndexNode2::Kind::kFunction, kFoo);
  auto foo_var_node = root.AddChild(IndexNode2::Kind::kVar, kFoo);

  const char kBar[] = "Bar";
  auto foo_bar_type_func = foo_type_node->AddChild(IndexNode2::Kind::kFunction, kBar);
  auto foo_bar_ns_func = foo_ns_node->AddChild(IndexNode2::Kind::kFunction, kBar);

  IndexWalker2 walker(&index);
  IndexWalker2::Stage expected_root{&root};
  EXPECT_TRUE(EqualsElements(walker.current(), expected_root));

  // Walking into "Foo" should identify all 4 categories of thing.
  EXPECT_TRUE(walker.WalkInto(ParsedIdentifierComponent(kFoo)));
  IndexWalker2::Stage expected_foo{foo_type_node, foo_ns_node, foo_func_node, foo_var_node};
  EXPECT_TRUE(EqualsElements(walker.current(), expected_foo));

  // Walking into "Bar" from there should narrow down to two.
  EXPECT_TRUE(walker.WalkInto(ParsedIdentifierComponent(kBar)));
  IndexWalker2::Stage expected_foo_bar{foo_bar_type_func, foo_bar_ns_func};
  EXPECT_TRUE(EqualsElements(walker.current(), expected_foo_bar));

  // Walking into something that doesn't exist reports failure and stays in the same place.
  EXPECT_FALSE(walker.WalkInto(ParsedIdentifierComponent("Nonexistant")));
  EXPECT_TRUE(EqualsElements(walker.current(), expected_foo_bar));

  // Walk up should give the same results.
  EXPECT_TRUE(walker.WalkUp());
  EXPECT_TRUE(EqualsElements(walker.current(), expected_foo));
  EXPECT_TRUE(walker.WalkUp());
  EXPECT_TRUE(EqualsElements(walker.current(), expected_root));

  // Going up above the root fails and does nothing.
  EXPECT_FALSE(walker.WalkUp());
  EXPECT_TRUE(EqualsElements(walker.current(), expected_root));
}

}  // namespace zxdb
