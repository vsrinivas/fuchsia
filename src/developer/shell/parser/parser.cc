// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include "src/developer/shell/parser/ast.h"

namespace shell::parser {

std::shared_ptr<ast::Node> Parse(std::string_view text) {
  return std::make_shared<ast::Error>(0, text.size(), "Parser not yet implemented.");
}

}  // namespace shell::parser
