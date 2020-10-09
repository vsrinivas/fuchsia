// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fit/function.h"
#include "src/developer/shell/parser/parse_result.h"

#ifndef SRC_DEVELOPER_SHELL_PARSER_ERROR_H_
#define SRC_DEVELOPER_SHELL_PARSER_ERROR_H_

namespace shell::parser {

// Handle an error by skipping some parsed data. If the skip parser fails the error handling fails.
fit::function<ParseResult(ParseResult)> ErSkip(std::string_view message,
                                               fit::function<ParseResult(ParseResult)> skip_parser);

// Insert an error into the parse stream.
fit::function<ParseResult(ParseResult)> ErInsert(std::string_view message);

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_ERROR_H_
