// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_

#include <deque>
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
    // Holds function pointers for an identify case type. For example,
    // for "UpperCamelCase" type, |matches| points to is_upper_camel_case()
    // and |convert| points to to_upper_camel_case().
    struct CaseType {
        fit::function<bool(std::string)> matches;
        fit::function<std::string(std::string)> convert;
    };

    // Holds information about a nesting context in a FIDL file, for checks
    // that must compare information about the context with information about
    // a nested entity. The outer-most context is the FIDL file itself
    // (including the file's declared library name). Contexts nested in a
    // file's context include type definitions with nested entities, such as
    // enum, bits, struct, table, union, and xunion.
    class Context {
    public:
        struct RepeatsContextNames;

        Context(std::string type, std::string id, CheckDef context_check)
            : type_(type), id_(id), context_check_(context_check) {}

        // Enables move construction and assignment
        Context(Context&& rhs) = default;
        Context& operator=(Context&&) = default;

        // no copy or assign (move-only or pass by reference)
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        std::string type() const { return type_; }

        std::string id() const { return id_; }

        // A |vector| of information about potential violations of the FIDL rubric
        // rule that prohibits repeating names from the outer type or library.
        // Exceptions to this rule cannot be determined until all nested identifiers
        // are reviewed, so this vector saves the required information until that
        // time.
        std::vector<RepeatsContextNames>& name_repeaters() { return name_repeaters_; }

        const std::set<std::string>& words() {
            if (words_.empty()) {
                auto words = utils::id_to_words(id_);
                words_.insert(words.begin(), words.end());
            }
            return words_;
        }

        const CheckDef& context_check() const { return context_check_; }

        template <typename... Args>
        void AddRepeatsContextNames(Args&&... args) {
            name_repeaters_.emplace_back(args...);
        }

        // Stores minimum information needed to construct a |Finding| if a
        // nested identifier repeats names from one of its contexts.
        // Determination is deferred until all nested identifiers are evaluated
        // because some cases of repeated names are allowed if the repeated
        // names help differentiate two identifiers that represent different
        // parts of the concept represented by the context identifier.
        struct RepeatsContextNames {
            RepeatsContextNames(std::string a_type, SourceLocation a_location,
                                std::set<std::string> a_repeats)
                : type(a_type), location(a_location), repeats(a_repeats) {}

            const std::string type;
            const SourceLocation location;
            const std::set<std::string> repeats;
        };

    private:
        std::string type_;
        std::string id_;
        std::set<std::string> words_;
        CheckDef context_check_;
        std::vector<RepeatsContextNames> name_repeaters_;
    };

    const std::set<std::string>& permitted_library_prefixes() const;
    std::string permitted_library_prefixes_as_string() const;

    CheckDef DefineCheck(std::string check_id,
                         std::string message_template);

    template <typename... Args>
    Finding& AddFinding(Args&&... args) const;

    const Finding& AddFinding(
        SourceLocation location,
        const CheckDef& check,
        Substitutions substitutions = {},
        std::string suggestion_template = "",
        std::string replacement_template = "") const;

    template <typename SourceElementSubtypeRefOrPtr>
    const Finding& AddFinding(
        const SourceElementSubtypeRefOrPtr& element,
        const CheckDef& check,
        Substitutions substitutions = {},
        std::string suggestion_template = "",
        std::string replacement_template = "") const;

    const Finding& AddRepeatedNameFinding(
        const Context& context,
        const Context::RepeatsContextNames& name_repeater) const;

    // If a finding was added, return a pointer to that finding.
    const Finding* CheckCase(std::string type,
                             const std::unique_ptr<raw::Identifier>& identifier,
                             const CheckDef& check_def, const CaseType& case_type);

    // CheckRepeatedName() does not add Finding objects immediately. It checks for
    // potential violations, but must wait until ExitContext() so the potential
    // violation can be compared to its peers.
    void CheckRepeatedName(std::string type,
                           const std::unique_ptr<raw::Identifier>& id);

    template <typename... Args>
    void EnterContext(Args&&... args) {
        context_stack_.emplace_front(args...);
    }

    // Pops the context stack. If any contained types repeat names from the
    // context, this function compares the nested identifiers with each other.
    // If two nested identifiers repeat different names from the context,
    // assume the repeated names were necessary in order to disambiguate the
    // concepts represented by each of the nested entities. If not, add Finding
    // objects for violating the repeated name rule.
    void ExitContext();

    std::deque<Context> context_stack_;

    LintingTreeCallbacks callbacks_;
    std::set<std::string> permitted_library_prefixes_;
    std::set<std::string> stop_words_;

    // All check types created in during |Linter| construction. The |std::set|
    // ensures each CheckDef has a unique |id|, and an iterator will traverse
    // the set in lexicographical order.
    std::set<CheckDef> checks_;

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
