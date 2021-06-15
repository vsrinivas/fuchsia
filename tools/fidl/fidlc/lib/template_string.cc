// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <regex>
#include <sstream>

#include <fidl/template_string.h>
#include <fidl/utils.h>

namespace fidl {

std::string TemplateString::Substitute(Substitutions substitutions, bool remove_unmatched, bool with_randomized) const {
  std::ostringstream os;
  std::smatch match;

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kRegexReplaceable =
      new std::regex(R"((.?)((?:\$\{([A-Z_][A-Z0-9_]*)\})|(?:\$([A-Z_][A-Z0-9_]*))))");

  int match_index = 1;
  static const int kPrecedingChar = match_index++;
  static const int kVarToken = match_index++;
  static const int kBracedVar = match_index++;
  static const int kUnbracedVar = match_index++;

  auto str = str_;
  while (std::regex_search(str, match, *kRegexReplaceable)) {
    os << match.prefix();
    if (match[kPrecedingChar] == "$") {
      os << std::string(match[0]).substr(1);  // escaped "$"
    } else {
      if (match[kPrecedingChar].matched) {
        os << match[kPrecedingChar];
      }
      std::string replaceable = match[kBracedVar].matched ? match[kBracedVar] : match[kUnbracedVar];
      if (substitutions.find(replaceable) != substitutions.end()) {
        // TODO(fxbug.dev/70247): Delete this
        std::visit(fidl::utils::matchers{
              [&](const std::string& str) -> void {
                os << str;
              },
              [&](const SubstitutionWithRandom& with_rand) -> void {
                os << with_rand.value;
                if (with_randomized)
                  os << with_rand.random;
              },
            },
            substitutions[replaceable]);
      } else if (!remove_unmatched) {
        os << match[kVarToken];
      }
    }
    str = match.suffix().str();
  }
  os << str;

  return os.str();
}

// TODO(fxbug.dev/70247): Delete this
TemplateString TemplateString::Unsubstitute(std::string& input, const Substitutions& substitutions) {
  for (const auto& substitution : substitutions) {
    const auto& with_rand = std::get<SubstitutionWithRandom>(substitution.second);
    const auto key = "${" + substitution.first + "}";
    const auto search_for = with_rand.value + with_rand.random;
    for (size_t pos = 0; pos != std::string::npos && pos < input.size(); pos += key.size()) {
      pos = input.find(search_for, pos);
      if (pos == std::string::npos)
        break;
      input.replace(pos, search_for.size(), key);
    }
  }

  return TemplateString(TemplateString(input));
}

}  // namespace fidl
