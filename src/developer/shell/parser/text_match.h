// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fit/function.h"
#include "src/developer/shell/parser/parse_result.h"

#ifndef SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
#define SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_

namespace shell::parser {

// Produce a parser which parses any single character from the given list.
fit::function<ParseResultStream(ParseResultStream)> AnyChar(const std::string& name,
                                                            std::string_view chars);

// Produce a parser which parses any single character not in the given list.
fit::function<ParseResultStream(ParseResultStream)> AnyCharBut(const std::string& name,
                                                               std::string_view chars);

// Produce a parser which parses any single character.
ParseResultStream AnyChar(ParseResultStream prefixes);

// Similar to AnyChar but the input string is a regex style range group like "a-zA-Z0-9".
fit::function<ParseResultStream(ParseResultStream)> CharGroup(const std::string& name,
                                                              std::string_view chars);

// Produce a parser to parse a fixed text string.
fit::function<ParseResultStream(ParseResultStream)> Token(const std::string& token);

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
