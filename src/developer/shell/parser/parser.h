// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_PARSER_H_
#define SRC_DEVELOPER_SHELL_PARSER_PARSER_H_

#include <string_view>

namespace shell::parser {
namespace ast {
class Node;
}

std::shared_ptr<ast::Node> Parse(std::string_view text);

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_PARSER_H_
