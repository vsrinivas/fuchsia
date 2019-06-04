// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/semantic_tree_parser.h"

#include <lib/gtest/test_loop_fixture.h>

#include <vector>

namespace accessibility_test {
using fuchsia::accessibility::semantics::Node;

const std::string kFileNotExistPath = "/some/random/path";
const std::string kSemanticTreePath = "/pkg/data/semantic_tree_odd_nodes.json";
const std::string kFileNotParseablePath =
    "/pkg/data/semantic_tree_not_parseable.json";

// Unit Test for garnet/bin/a11y/tests/semantic_tree_parser.h
class SemanticTreeParserTest : public gtest::TestLoopFixture {
 public:
  SemanticTreeParser semantic_tree_parser_;
};

TEST_F(SemanticTreeParserTest, FileNotExist) {
  std::vector<Node> nodes;
  ASSERT_FALSE(
      semantic_tree_parser_.ParseSemanticTree(kFileNotExistPath, &nodes));
  ASSERT_TRUE(nodes.size() == 0);
}

TEST_F(SemanticTreeParserTest, SuccessfullyParseFile) {
  std::vector<Node> nodes;
  ASSERT_TRUE(
      semantic_tree_parser_.ParseSemanticTree(kSemanticTreePath, &nodes));
  ASSERT_TRUE(nodes.size() == 7);
}

TEST_F(SemanticTreeParserTest, ParsingFailed) {
  std::vector<Node> nodes;
  ASSERT_FALSE(
      semantic_tree_parser_.ParseSemanticTree(kFileNotParseablePath, &nodes));
  ASSERT_TRUE(nodes.size() == 0);
}

}  // namespace accessibility_test
