// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_

#include <list>
#include <set>

#include <fidl/check_def.h>
#include <fidl/findings.h>
#include <fidl/linting_tree_callbacks.h>
#include <fidl/tree_visitor.h>
#include <fidl/utils.h>

namespace fidl {
namespace linter {

// The primary business logic for lint-checks on a FIDL file is implemented in
// the |Linter| class.
class Linter {

public:
    // On initialization, the |Linter| constructs the |CheckDef|
    // objects with associated check logic (lambdas registered via
    // |CheckCallbacks|).
    Linter();

    // Calling Lint() invokes the callbacks for elements
    // of the given |SourceFile|. If a check fails, the callback generates a
    // |Finding| and adds it to the given Findings (vector of Finding).
    // Lint() is not thread-safe.
    // Returns true if no new findings were generated.
    bool Lint(std::unique_ptr<raw::File> const& parsed_source,
              Findings* findings);

private:
    struct CaseType {
        fit::function<bool(std::string)> matches;
        fit::function<std::string(std::string)> convert;
    };

    class Context {
    public:
        Context(std::string type, std::string id, CheckDef context_check)
            : type_(type), id_(id), context_check_(context_check) {}

        // Enables move construction and assignment
        Context(Context&& rhs) = default;
        Context& operator=(Context&&) = default;

        // no copy or assign (move-only or pass by reference)
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        std::string type() { return type_; }

        std::string id() { return id_; }

        const std::set<std::string>& words() {
            if (words_.empty()) {
                auto words = utils::id_to_words(id_);
                words_.insert(words.begin(), words.end());
            }
            return words_;
        }

        const CheckDef& context_check() { return context_check_; }

    private:
        std::string type_;
        std::string id_;
        std::set<std::string> words_;
        CheckDef context_check_;
    };

    const std::set<std::string>& permitted_library_prefixes() const;
    std::string permitted_library_prefixes_as_string() const;

    CheckDef DefineCheck(std::string check_id,
                         std::string message_template);

    template <typename... Args>
    Finding& AddFinding(Args&&... args) const;

    template <typename SourceElementSubtypeRefOrPtr>
    const Finding& AddFinding(
        const SourceElementSubtypeRefOrPtr& element,
        const CheckDef& check,
        Substitutions substitutions = {},
        std::string suggestion_template = "",
        std::string replacement_template = "") const;

    std::set<std::string> permitted_library_prefixes_ = {
        "fuchsia",
        "fidl",
        "test",
    };

    const Finding* CheckCase(std::string type,
                             const std::unique_ptr<raw::Identifier>& identifier,
                             const CheckDef& check_def, const CaseType& case_type);
    const Finding* CheckRepeatedName(std::string type,
                                     const std::unique_ptr<raw::Identifier>& id);

    template <typename... Args>
    void EnterContext(Args&&... args) {
        context_stack_.emplace_front(args...);
    }

    void ExitContext() {
        context_stack_.pop_front();
    }

    std::list<Context> context_stack_;

    std::vector<CheckDef> checks_;
    LintingTreeCallbacks callbacks_;

    CaseType lower_snake_{utils::is_lower_snake_case,
                          utils::to_lower_snake_case};
    CaseType upper_snake_{utils::is_upper_snake_case,
                          utils::to_upper_snake_case};
    CaseType upper_camel_{utils::is_upper_camel_case,
                          utils::to_upper_camel_case};

    // Pointer to the current "Findings" object, passed to the Lint() method,
    // for the diration of the Visit() to lint a given FIDL file. When the
    // Visit() is over, |current_findings_| is reset to nullptr.
    // As a result, Lint() is single-threaded. The variable could be changed
    // to thread-local, but in general this class has not been reviewed for
    // thread-safety characteristics, and it is not currently deemed necessary.
    Findings* current_findings_ = nullptr;
};

} // namespace linter
} // namespace fidl
#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
