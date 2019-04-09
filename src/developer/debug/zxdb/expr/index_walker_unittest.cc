// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/index_walker.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index.h"

namespace zxdb {

TEST(IndexWalker, ComponentMatchesNameOnly) {
  Identifier::Component foo_comp(ExprToken(),
                                 ExprToken(ExprTokenType::kName, "Foo", 0));
  Identifier::Component foo_template_comp(
      ExprToken(), ExprToken(ExprTokenType::kName, "Foo", 0),
      ExprToken(ExprTokenType::kLess, "<", 10), {"A", "b"},
      ExprToken(ExprTokenType::kGreater, ">", 100));

  // Simple name-only comparisons.
  EXPECT_TRUE(IndexWalker::ComponentMatchesNameOnly("Foo", foo_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatchesNameOnly("FooBar", foo_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatchesNameOnly("Fo2", foo_comp));

  // Component has a template, the index string doesn't.
  EXPECT_TRUE(IndexWalker::ComponentMatchesNameOnly("Foo", foo_template_comp));

  // Component has no template, the index does.
  EXPECT_TRUE(
      IndexWalker::ComponentMatchesNameOnly("Foo < C >", foo_template_comp));
}

TEST(IndexWalker, ComponentMatchesTemplateOnly) {
  Identifier::Component foo_comp(ExprToken(),
                                 ExprToken(ExprTokenType::kName, "Foo", 0));
  Identifier::Component foo_template_comp(
      ExprToken(), ExprToken(ExprTokenType::kName, "Foo", 0),
      ExprToken(ExprTokenType::kLess, "<", 10), {"A", "b"},
      ExprToken(ExprTokenType::kGreater, ">", 100));
  Identifier::Component foo_empty_template_comp(
      ExprToken(), ExprToken(ExprTokenType::kName, "Foo", 0),
      ExprToken(ExprTokenType::kLess, "<", 10), {},
      ExprToken(ExprTokenType::kGreater, ">", 100));

  // Neither inputs have templates (should be a match).
  EXPECT_TRUE(IndexWalker::ComponentMatchesTemplateOnly("Foo", foo_comp));

  // Template match but with different whitespace.
  EXPECT_TRUE(IndexWalker::ComponentMatchesTemplateOnly("Foo < A,  b > ",
                                                        foo_template_comp));

  // One has a template but the other doesn't.
  EXPECT_FALSE(
      IndexWalker::ComponentMatchesTemplateOnly("Foo", foo_template_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatchesTemplateOnly("Foo<C>", foo_comp));

  // Empty template doesn't match no template.
  EXPECT_FALSE(IndexWalker::ComponentMatchesTemplateOnly("Foo<>", foo_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatchesTemplateOnly(
      "Foo", foo_empty_template_comp));
}

// Most cases are tested by ComponentMatchesNameOnly and ...TemplateOnly above.
TEST(IndexWalker, ComponentMatches) {
  Identifier::Component foo_comp(ExprToken(),
                                 ExprToken(ExprTokenType::kName, "Foo", 0));
  Identifier::Component foo_template_comp(
      ExprToken(), ExprToken(ExprTokenType::kName, "Foo", 0),
      ExprToken(ExprTokenType::kLess, "<", 10), {"A", "b"},
      ExprToken(ExprTokenType::kGreater, ">", 100));

  EXPECT_TRUE(IndexWalker::ComponentMatches("Foo", foo_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatches("Foo<>", foo_comp));
  EXPECT_FALSE(IndexWalker::ComponentMatches("Foo<>", foo_template_comp));
  EXPECT_TRUE(IndexWalker::ComponentMatches("Foo <A,b >", foo_template_comp));
}

TEST(IndexWalker, IsIndexStringBeyondName) {
  // Identity comparison.
  EXPECT_FALSE(IndexWalker::IsIndexStringBeyondName("Foo", "Foo"));

  // Index nodes clearly before.
  EXPECT_FALSE(IndexWalker::IsIndexStringBeyondName("Fo", "Foo"));
  EXPECT_FALSE(IndexWalker::IsIndexStringBeyondName("Foa", "Foo"));

  // Index nodes clearly after.
  EXPECT_TRUE(IndexWalker::IsIndexStringBeyondName("FooBar", "Foo"));
  EXPECT_TRUE(IndexWalker::IsIndexStringBeyondName("Foz", "Foo"));
  EXPECT_TRUE(IndexWalker::IsIndexStringBeyondName("Fz", "Foo"));

  // Templates in the index could has "not beyond".
  EXPECT_FALSE(IndexWalker::IsIndexStringBeyondName("Foo<a>", "Foo"));
}

TEST(IndexWalker, WalkInto) {
  ModuleSymbolIndex index;
  auto& root = index.root();
  auto foo_node = root.AddChild("Foo");
  root.AddChild("Foo<Bar>");

  // These template names are non-canonical so we can verify the correct
  // comparisons happen.
  foo_node->AddChild("Bar< int >");
  auto bar_int_char_node = foo_node->AddChild("Bar< int,char >");

  // There could also be a non-template somewhere with the same name.
  auto bar_node = foo_node->AddChild("Bar");

  // These nodes start with the prefix "Bar" for when we're searching. We test
  // things that will compare before and after "Bar<" ('9' before, 'f' after).
  auto barf_node = foo_node->AddChild("Barf<int>");
  auto bar9_node = foo_node->AddChild("Bar9<int>");

  IndexWalker walker(&index);
  EXPECT_EQ(&root, walker.current());

  // Walking up at this point should be a no-op.
  EXPECT_FALSE(walker.WalkUp());
  EXPECT_EQ(&root, walker.current());

  // Walk to the "Foo" component.
  EXPECT_TRUE(walker.WalkInto(Identifier::Component(false, "Foo")));
  EXPECT_EQ(foo_node, walker.current());

  // Walk to the "NotPresent" component, this time specify a separator. The
  // current location should be unchanged.
  EXPECT_FALSE(walker.WalkInto(Identifier::Component(true, "NotFound")));
  EXPECT_EQ(foo_node, walker.current());

  // Walk to the "Bar<int,char>" identifier.
  auto [err1, bar_int_char] = Identifier::FromString("Bar < int , char >");
  EXPECT_FALSE(err1.has_error()) << err1.msg();
  EXPECT_TRUE(walker.WalkInto(bar_int_char));
  EXPECT_EQ(bar_int_char_node, walker.current());

  // Walk back up to "Foo".
  EXPECT_TRUE(walker.WalkUp());
  EXPECT_EQ(foo_node, walker.current());

  // Walk to the "Bar" node.
  EXPECT_TRUE(walker.WalkInto(Identifier::Component(false, "Bar")));
  EXPECT_EQ(bar_node, walker.current());

  // Parse the Barf identifier for the following two tests. This one has a
  // toplevel scope.
  auto [err2, barf] = Identifier::FromString("::Foo::Barf<int>");
  EXPECT_FALSE(err2.has_error()) << err2.msg();

  // Walk to the "Foo::Bar9<int>" with copying the walker.
  {
    IndexWalker nested_walker(walker);
    auto [err2, bar9] = Identifier::FromString(":: Foo :: Bar9 < int >");
    EXPECT_FALSE(err2.has_error()) << err2.msg();
    EXPECT_TRUE(nested_walker.WalkInto(bar9));
    EXPECT_EQ(bar9_node, nested_walker.current());
  }

  // Walking from the root into the barf template should work.
  EXPECT_TRUE(walker.WalkInto(barf));
  EXPECT_EQ(barf_node, walker.current());
}

}  // namespace zxdb
