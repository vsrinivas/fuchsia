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
    auto skip = skip_parser(prefix);

    if (skip && skip.errors() == prefix.errors()) {
      auto pos = message.find(MATCH_REPLACE);
      auto match_size = skip.offset() - prefix.offset();

      if (pos != std::string::npos) {
        std::string modified_message(message);

        modified_message.replace(pos, MATCH_REPLACE.size(),
                                 skip.unit().substr(prefix.offset(), match_size));

        return prefix.Skip(match_size, modified_message);
      }

      return prefix.Skip(match_size, message);
    }

    return ParseResult::kEnd;
  };
}

fit::function<ParseResult(ParseResult)> ErInsert(std::string_view message) {
  return [message](ParseResult prefix) { return prefix.Expected(message); };
}

}  // namespace shell::parser
