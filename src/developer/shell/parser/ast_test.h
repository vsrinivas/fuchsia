// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_AST_TEST_H_
#define SRC_DEVELOPER_SHELL_PARSER_AST_TEST_H_

#include "src/developer/shell/parser/ast.h"

namespace shell::parser::ast {

class TestNode : public Nonterminal {
 public:
  TestNode(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return ""; }
};

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_TEST_H_
