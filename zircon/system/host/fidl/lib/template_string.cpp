// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/template_string.h>

#include <iostream>
#include <regex>
#include <sstream>

namespace fidl {

std::string TemplateString::Substitute(Substitutions substitutions,
                                       bool remove_unmatched) const {
    std::ostringstream os;
    std::smatch match;

    static const std::regex kRegexReplaceable(
        R"((.?)(?:(?:\$\{([A-Z_][A-Z0..9_]*)\})|(?:\$([A-Z_][A-Z0..9_]*))))");
    int match_index = 1;
    static const int kPrecedingChar = match_index++;
    static const int kBracedVar = match_index++;
    static const int kUnbracedVar = match_index++;

    auto str = str_;
    while (std::regex_search(str, match, kRegexReplaceable)) {
        os << match.prefix();
        if (match[kPrecedingChar] == "$") {
            os << std::string(match[0]).substr(1); // escaped "$"
        } else {
            if (match[kPrecedingChar].matched) {
                os << match[kPrecedingChar];
            }
            std::string replaceable = match[kBracedVar].matched
                                          ? match[kBracedVar]
                                          : match[kUnbracedVar];
            if (substitutions.find(replaceable) != substitutions.end()) {
                os << substitutions[replaceable];
            } else if (!remove_unmatched) {
                os << match[0];
            }
        }
        str = match.suffix().str();
    }
    os << str;

    return os.str();
}

} // namespace fidl
