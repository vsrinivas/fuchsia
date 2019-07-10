// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/expr/format_node.h"

namespace zxdb {

namespace {

void FillBaseTypeNode(const std::string& type_name, const std::string& description,
                      FormatNode* node) {
  node->set_state(FormatNode::kDescribed);
  node->set_type(type_name);
  node->set_description_kind(FormatNode::kBaseType);
  node->set_description(description);
}

}  // namespace

TEST(FormatNodeConsole, SimpleValue) {
  FormatNode node;
  FillBaseTypeNode("int", "54", &node);
  ConsoleFormatNodeOptions options;

  // Bare value.
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("kNormal \"54\"", out.GetDebugString());

  // Bare value with types forced on.
  ConsoleFormatNodeOptions type_options;
  type_options.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ(R"(kComment "(int) ", kNormal "54")", out.GetDebugString());

  // Named value.
  node.set_name("foo");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ(R"(kVariable "foo", kNormal " = 54")", out.GetDebugString());

  // Named value with types forced on.
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ(R"(kComment "(int) ", kVariable "foo", kNormal " = 54")", out.GetDebugString());
}

TEST(FormatNodeConsole, Collection) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("MyClass");
  node.set_description_kind(FormatNode::kCollection);
  node.set_description("This description is not displayed for a collection.");

  ConsoleFormatNodeOptions options;

  // Empty collection.
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{}", out.AsString());

  // Add some children.
  auto child = std::make_unique<FormatNode>("a");
  FillBaseTypeNode("int", "42", child.get());
  node.children().push_back(std::move(child));

  child = std::make_unique<FormatNode>("b");
  FillBaseTypeNode("double", "3.14159", child.get());
  node.children().push_back(std::move(child));

  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{a = 42, b = 3.14159}", out.AsString());

  // With types forced.
  options.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(MyClass) {(int) a = 42, (double) b = 3.14159}", out.AsString());
}

TEST(FormatNodeConsole, Array) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int[2]");
  node.set_description_kind(FormatNode::kArray);
  node.set_description("This description is not displayed for arrays.");

  // Empty array.
  ConsoleFormatNodeOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{}", out.AsString());

  // Add some children.
  auto child = std::make_unique<FormatNode>("[0]");
  FillBaseTypeNode("int", "42", child.get());
  node.children().push_back(std::move(child));

  child = std::make_unique<FormatNode>("[1]");
  FillBaseTypeNode("int", "137", child.get());
  node.children().push_back(std::move(child));

  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{42, 137}", out.AsString());

  // Truncated array.
  node.children().push_back(std::make_unique<FormatNode>("..."));
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{42, 137, ...}", out.AsString());

  // With types forced on.
  ConsoleFormatNodeOptions type_options;
  type_options.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int[2]) {42, 137, ...}", out.AsString());
}

TEST(FormatNodeConsole, Pointer) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int*");
  node.set_description_kind(FormatNode::kPointer);
  node.set_description("0x12345678");

  // Pointed to value. Don't fill it in yet.
  auto child = std::make_unique<FormatNode>("*");
  child->set_state(FormatNode::kUnevaluated);
  node.children().push_back(std::move(child));

  // Print the bare pointer.
  ConsoleFormatNodeOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678", out.AsString());

  // Fill the pointed-to value.
  FillBaseTypeNode("int", "42", node.children()[0].get());

  // Print with the pointed-to value.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678 🡺 42", out.AsString());

  // Print with type information. Should only show on the pointer and not be duplicated on the
  // pointed-to value.
  ConsoleFormatNodeOptions type_options;
  type_options.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) 0x12345678 🡺 42", out.AsString());

  // Add a name.
  node.set_name("a");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = (*)0x12345678 🡺 42", out.AsString());
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) a = 0x12345678 🡺 42", out.AsString());
}

TEST(FormatNodeConsole, Reference) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int&");
  node.set_description_kind(FormatNode::kReference);
  node.set_description("0x12345678");

  // Pointed to value. Don't fill it in yet.
  auto child = std::make_unique<FormatNode>();
  child->set_state(FormatNode::kUnevaluated);
  node.children().push_back(std::move(child));

  // Print the bare reference.
  ConsoleFormatNodeOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(&)0x12345678", out.AsString());

  // Fill the pointed-to value.
  FillBaseTypeNode("int", "42", node.children()[0].get());

  // Print with the pointed-to value.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("42", out.AsString());

  // Print with type information. Should only show on the pointer and not be duplicated on the
  // pointed-to value.
  ConsoleFormatNodeOptions type_options;
  type_options.verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int&) 42", out.AsString());

  // Add a name.
  node.set_name("a");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = 42", out.AsString());
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int&) a = 42", out.AsString());
}

}  // namespace zxdb
