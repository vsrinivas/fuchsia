// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/linter.h"

#include <algorithm>
#include <sstream>

void IdentifierChecker::WarnOnMismatch(fidl::raw::Identifier& identifier) {
    std::string id(identifier.end_.data().data(), identifier.end_.data().size());
    if (!Check(id)) {
        std::string error = "Identifier\n    ";
        error.append(id);
        error.append("\nis not ");
        error.append(name_);
        error.append(" (Pattern: ");
        error.append(pattern_s_);
        error.append(")\n");
        std::optional<std::string> recommendation = Recommend(id);
        if (recommendation) {
            error.append("Did you mean:\n    ");
            error.append(*recommendation);
        }
        error_reporter_->ReportWarning(identifier.start_.location(), error);
    }
}

// Many, many people will either a) lower-case the first letter, or b) get the
// rules for acronyms wrong.  This tries to detect that situation and provide a
// useful suggestion.
std::optional<std::string> UpperCamelCaseChecker::Recommend(std::string& id) {
    std::string test = id;
    // Just uppercase the first letter.
    test[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(test[0])));

    // Look for a sequence of three or more uppercase letters.  Lowercase
    // everything between the first and last (0, n).
    std::smatch sm;
    std::regex re("[A-Z][A-Z]+[A-Z]");
    while (std::regex_search(test, sm, re)) {
        std::string new_test = sm.prefix();
        std::string match = sm[0];
        new_test += match[0];
        for (size_t i = 1; i + 1 < match.size(); i++) {
            new_test += static_cast<char>(
                std::tolower(static_cast<unsigned char>(match[i])));
        }
        new_test += match[match.size() - 1];
        new_test.append(sm.suffix());
        test = new_test;
    }

    // If it passes, it's a good recommendation.  Maybe.
    if (Check(test)) {
        return test;
    }
    return {};
}

LintingTreeVisitor::LintingTreeVisitor(fidl::ErrorReporter* error_reporter)
    : tokenizer_(error_reporter),
      legal_library_name_("a legal library name", "^((?!(common|service|util|base|f.l|zx[a-z]*)).)*$", tokenizer_, error_reporter),
      single_identifier_("single identifier", "[a-z][a-z0-9]*", tokenizer_, error_reporter),
      upper_snake_case_(tokenizer_, error_reporter),
      lower_snake_case_("lower snake case", "[a-z0-9]+(_[a-z0-9]+)*", tokenizer_, error_reporter),
      upper_camel_case_(tokenizer_, error_reporter) {}

std::vector<std::string> IdentifierTokenizer::Tokenize(
    std::string& identifier) const {
    // This makes a best effort attempt to break the identifier into separate
    // tokens.
    std::vector<std::string> vs;
    size_t i = 0;
    if (identifier[0] == 'k' && upper_camel_case_.Check(identifier.substr(1))) {
        // Weird special case for kCamelCase, used by C++ consts, and
        // erroneously for FIDL consts sometimes.
        i++;
    }

    // Right now, the strategy is to have three categories:
    // 1. upper
    // 2. lower
    // 3. non-letter
    // You are allowed to go from upper to lower, but no other transition is
    // allowed. For each character in the string, identify its category. If the
    // current token category can transition to the new character's category,
    // append the character to the token, otherwise create a new token
    // consisting of just the character.
    enum State { UPPER,
                 LOWER,
                 NON_LETTER };
    State previous = NON_LETTER;
    while (i < identifier.size()) {
        if (std::isupper(identifier[i])) {
            if (previous != UPPER) {
                vs.push_back(std::string(1, identifier[i]));
            } else {
                vs.back().push_back(identifier[i]);
            }
            previous = UPPER;
        } else if (std::islower(identifier[i]) || std::isdigit(identifier[i])) {
            if (previous != UPPER && previous != LOWER) {
                vs.push_back(std::string(1, identifier[i]));
            } else {
                vs.back().push_back(identifier[i]);
            }
            previous = LOWER;
        } else {
            previous = NON_LETTER;
        }
        i++;
    }

    return vs;
}

// Break into tokens, recommend UpperSnakeCase version of tokens.
std::optional<std::string> UpperSnakeCaseChecker::Recommend(
    std::string& identifier) {
    std::vector<std::string> tokens = tokenizer_.Tokenize(identifier);

    // identifiers must start with [a-zA-Z], which means there should always be
    // some token.
    assert(tokens.size() != 0);

    std::string recommendation = "";
    for (size_t i = 0; i < tokens.size(); i++) {
        std::string token = tokens[i];
        std::transform(token.begin(), token.end(), token.begin(), ::toupper);
        recommendation.append(token);
        if (i + 1 != tokens.size()) {
            recommendation += '_';
        }
    }
    return recommendation;
}

void LintingTreeVisitor::OnFile(std::unique_ptr<fidl::raw::File> const& element) {
    for (auto& id : element->library_name->components) {
        single_identifier_.WarnOnMismatch(*id);
        legal_library_name_.WarnOnMismatch(*id);
    }
    element->Accept(*this);
}

void LintingTreeVisitor::OnConstDeclaration(std::unique_ptr<fidl::raw::ConstDeclaration> const& element) {
    upper_snake_case_.WarnOnMismatch(*(element->identifier));
    element->Accept(*this);
}

void LintingTreeVisitor::OnInterfaceDeclaration(std::unique_ptr<fidl::raw::InterfaceDeclaration> const& element) {
    upper_camel_case_.WarnOnMismatch(*(element->identifier));
    element->Accept(*this);
}

void LintingTreeVisitor::OnUsing(std::unique_ptr<fidl::raw::Using> const& element) {
    if (element->maybe_alias) {
        lower_snake_case_.WarnOnMismatch(*(element->maybe_alias));
    }
    element->Accept(*this);
}
