// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/error.h"

namespace shell::parser {
namespace {

const std::string MATCH_REPLACE("%MATCH%");

}  // namespace

fit::function<ParseResult(ParseResult)> ErSkip(
    std::string_view message, fit::function<ParseResult(ParseResult)> skip_parser) {
  return [message, skip_parser = std::move(skip_parser)](ParseResult prefix) {
    auto skip = skip_parser(ParseResult(prefix.tail()));

    if (skip && skip.error_score() == 0) {
      auto pos = message.find(MATCH_REPLACE);

      if (pos != std::string::npos) {
        std::string modified_message(message);

        modified_message.replace(pos, MATCH_REPLACE.size(), skip.unit().substr(0, skip.offset()));

        return prefix.Skip(skip.offset(), modified_message);
      }

      return prefix.Skip(skip.offset(), message);
    }

    return ParseResult::kEnd;
  };
}

fit::function<ParseResult(ParseResult)> ErInsert(std::string_view message, size_t length) {
  return [message, length](ParseResult prefix) { return prefix.Expected(length, message); };
}

fit::function<ParseResult(ParseResult)> ErInsert(std::string_view message, std::string_view token) {
  return ErInsert(message, token.size());
}

}  // namespace shell::parser
