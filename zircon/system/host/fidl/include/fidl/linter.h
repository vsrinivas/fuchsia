// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_

#include <optional>
#include <regex>

#include "error_reporter.h"
#include "tree_visitor.h"

class IdentifierTokenizer;

// This class allows users to check an identifier to see whether it matches a
// given regexp.  It contains additional logic to work with the linting tool.
class IdentifierChecker {
public:
    // |name| is a descriptive name for this check (e.g., "upper snake case").
    //
    // |pattern_s| is the (std::regex-friendly) pattern that we check the
    // identifier against for compliance.
    //
    // |tokenizer| is an instance of IdentifierToken.  Implementations of this
    // class may use it to enable the Recommend() method to provide useful
    // values when input is simply capitalized incorrectly (for example,
    // kFooBarBaz can be broken into Foo, Bar, and Baz, at which point we might
    // know that we want FOO_BAR_BAZ instead.
    //
    // |error_reporter| is the object to use to log warnings, when non-compliant
    // identifiers are found.
    IdentifierChecker(const std::string& name,
                      const std::string& pattern_s,
                      const IdentifierTokenizer& tokenizer,
                      fidl::ErrorReporter* error_reporter)
        : name_(name),
          pattern_s_(pattern_s),
          pattern_(pattern_s),
          tokenizer_(tokenizer),
          error_reporter_(error_reporter) {}

    // Check returns true if the identifier matches the pattern given to
    // IdentifierChecker, and false otherwise.
    virtual bool Check(const std::string& identifier) const {
        std::cmatch match;
        return std::regex_match(identifier.c_str(), match, pattern_);
    }

    // WarnOnMismatch logs a warning to the error reporter if the given
    // identifier does not match the pattern given to the constructor.
    void WarnOnMismatch(fidl::raw::Identifier& identifier);

    // Returns a user-friendly name for this check (e.g., upper camel case).
    const std::string Name() { return name_; }

    // Potentially returns a recommended alternative token to the given
    // identifier.  The default implementation returns an empty optional -
    // implementations of this class may choose to implement recommendations.
    virtual std::optional<std::string> Recommend(std::string& id) {
        return {};
    }

protected:
    const std::string name_;

    const std::string pattern_s_;
    std::regex pattern_;

    const IdentifierTokenizer& tokenizer_;
    fidl::ErrorReporter* error_reporter_;
};

// An implementation of IdentifierChecker that checks for UpperCamelCase
// compliance.
class UpperCamelCaseChecker : public IdentifierChecker {
public:
    // Should probably special-case various allowable single-letter word
    // combinations.
    UpperCamelCaseChecker(const IdentifierTokenizer& tokenizer,
                          fidl::ErrorReporter* error_reporter)
        : IdentifierChecker("upper camel case", "(?:[A-Z][a-z0-9]+)+", tokenizer, error_reporter) {}

    std::optional<std::string> Recommend(std::string& id) override;
};

// An implementation of IdentifierChecker that checks for UPPER_SNAKE_CASE
// compliance.
class UpperSnakeCaseChecker : public IdentifierChecker {
public:
    UpperSnakeCaseChecker(const IdentifierTokenizer& tokenizer,
                          fidl::ErrorReporter* error_reporter)
        : IdentifierChecker("upper snake case", "[A-Z]+(_[A-Z0-9]+)*", tokenizer, error_reporter) {}

    std::optional<std::string> Recommend(std::string& id) override;
};

// This is a class whose implementations make an attempt to break the given
// identifier into individual tokens.  For example, CamelCaseIdentifier might be
// broken into a vector containing "Camel", "Case", and "Identifier".
class IdentifierTokenizer {
public:
    IdentifierTokenizer(fidl::ErrorReporter* error_reporter)
        : upper_camel_case_(*this, error_reporter){};

    std::vector<std::string> Tokenize(std::string& identifier) const;

private:
    const UpperCamelCaseChecker upper_camel_case_;
};

// A tree visitor that runs lint checks against the contents of each node.
// Nodes which own an identifier or a compound identifier should override the
// default visitor, in order to invoke contextual checks on those identifiers.
class LintingTreeVisitor : public fidl::raw::TreeVisitor {
public:
    LintingTreeVisitor(fidl::ErrorReporter* error_reporter);

    void OnFile(std::unique_ptr<fidl::raw::File> const& element) override;

    void OnConstDeclaration(std::unique_ptr<fidl::raw::ConstDeclaration> const& element) override;

    void OnInterfaceDeclaration(std::unique_ptr<fidl::raw::InterfaceDeclaration> const& element) override;

    void OnUsing(std::unique_ptr<fidl::raw::Using> const& element) override;

private:
    IdentifierTokenizer tokenizer_;
    IdentifierChecker legal_library_name_;
    IdentifierChecker single_identifier_;
    UpperSnakeCaseChecker upper_snake_case_;
    IdentifierChecker lower_snake_case_;
    UpperCamelCaseChecker upper_camel_case_;
};

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
