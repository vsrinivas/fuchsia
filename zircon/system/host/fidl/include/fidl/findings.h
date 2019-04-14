// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_H_

///////////////////////////////////////////////////////////////
// Even though this file is namespaced to "fidl::lint", it
// could be promoted to the "fidl" namespace in the future.
//
// findings.h and findings.cpp should not have any
// dependencies on the "Lint" process. They should be
// generic enough to be useful for capturing and reporting
// findings from other developer tools, such as fidlc.
///////////////////////////////////////////////////////////////

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fidl/source_location.h>

namespace fidl {

// A suggested change to address a linter |Finding|, with a human language
// description of the suggestion, and one or more potential |Replacement|
// values for referenced parts of the source.
struct Suggestion {
public:
    explicit Suggestion(std::string description)
        : description_(description) {}

    Suggestion(std::string description, std::string replacement)
        : description_(description), replacement_(replacement) {}

    // Enables move construction and assignment
    Suggestion(Suggestion&& rhs) = default;
    Suggestion& operator=(Suggestion&&) = default;

    // no copy or assign (move-only or pass by reference)
    Suggestion(const Suggestion&) = delete;
    Suggestion& operator=(const Suggestion&) = delete;

    // Describes the suggestion in human terms.
    inline const std::string& description() const { return description_; }
    inline const std::optional<std::string>& replacement() const {
        return replacement_;
    }

private:
    std::string description_;
    std::optional<std::string> replacement_;
};

class Finding {
public:
    // Construct a Finding with an analyzer-specific subcategory string (for
    // example, fidl-lint's check-id), SourceLocation, and message
    Finding(SourceLocation source_location,
            std::string subcategory, std::string message)
        : source_location_(source_location),
          subcategory_(subcategory), message_(message) {}

    // move constructor (required for emplace() functions)
    Finding(Finding&& rhs) = default;

    // Construct a new Suggestion with its constructor arguments, and add it to
    // the Finding.
    template <typename... Args>
    Suggestion& SetSuggestion(Args&&... args) {
        suggestion_.emplace(std::forward<Args>(args)...);
        return suggestion_.value();
    }

    // Returns a reference to a portion of a |SourceFile|, with supporting
    // methods to get the relative location of the reference within the file
    // (line and column), and StringView (substring) representing the characters
    // from reference start to end.
    inline const SourceLocation& source_location() const {
        return source_location_;
    }

    // Subcategory of the result (for example, fidl-lint's check-id). Used
    // to construct a Comment category, as described in the Tricium protobuf:
    //
    //   Category of the result, encoded as a path with the analyzer name as the
    //   root, followed by an arbitrary number of subcategories, for example
    //   "ClangTidy/llvm-header-guard".
    //
    // https://chromium.googlesource.com/infra/infra/+/refs/heads/master/go/src/infra/tricium/api/v1/data.proto
    inline const std::string& subcategory() const { return subcategory_; }

    // The annotation, as a human consumable text string.
    inline const std::string& message() const { return message_; }

    // An optional |Suggestion| to correct the issue (potentially with
    // a suggested |Replacement|).
    inline const std::optional<Suggestion>& suggestion() const {
        return suggestion_;
    }

private:
    SourceLocation source_location_;
    std::string subcategory_;
    std::string message_;
    std::optional<Suggestion> suggestion_;
};

using Findings = std::vector<Finding>;

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_H_
