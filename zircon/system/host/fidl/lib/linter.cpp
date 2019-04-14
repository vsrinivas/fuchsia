// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/linter.h>

#include <algorithm>
#include <iostream>
#include <regex>
#include <set>

#include <lib/fit/function.h>

#include <fidl/findings.h>
#include <fidl/raw_ast.h>
#include <fidl/utils.h>

namespace fidl {
namespace linter {

#define CHECK_CASE(CASE, IDENTIFIER)          \
    assert(IDENTIFIER != nullptr);            \
    std::string id = to_string(IDENTIFIER);   \
    if (!utils::is_##CASE##_case(id)) {       \
        linter.AddReplaceIdFinding(           \
            IDENTIFIER, check,                \
            id, utils::to_##CASE##_case(id)); \
    }

const std::set<std::string>& Linter::permitted_library_prefixes() const {
    return permitted_library_prefixes_;
}

std::string Linter::permitted_library_prefixes_as_string() const {
    std::ostringstream ss;
    bool first = true;
    for (auto& prefix : permitted_library_prefixes()) {
        if (!first) {
            ss << " | ";
        }
        ss << prefix;
        first = false;
    }
    return ss.str();
}

// Returns itself. Overloaded to support alternative type references by
// pointer and unique_ptr as needed.
static const fidl::raw::SourceElement& GetElementAsRef(
    const fidl::raw::SourceElement& source_element) {
    return source_element;
}

static const fidl::raw::SourceElement& GetElementAsRef(
    const fidl::raw::SourceElement* element) {
    return GetElementAsRef(*element);
}

// Returns the pointed-to element as a reference.
template <typename SourceElementSubtype>
const fidl::raw::SourceElement& GetElementAsRef(
    const std::unique_ptr<SourceElementSubtype>& element_ptr) {
    static_assert(
        std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
        "Template parameter type is not derived from SourceElement");
    return GetElementAsRef(element_ptr.get());
}

// Convert the SourceElement (start- and end-tokens within the SourceFile)
// to a StringView, spanning from the beginning of the start token, to the end
// of the end token. The three methods support classes derived from
// SourceElement, by reference, pointer, or unique_ptr.
static StringView to_string_view(const fidl::raw::SourceElement& element) {
    auto start_string = element.start_.data();
    const char* start_ptr = start_string.data();
    auto end_string = element.end_.data();
    const char* end_ptr = end_string.data() + end_string.size();
    size_t size = static_cast<size_t>(end_ptr - start_ptr);
    return StringView(start_ptr, size);
}

static StringView to_string_view(const fidl::raw::SourceElement* element) {
    return to_string_view(*element);
}

template <typename SourceElementSubtype>
StringView to_string_view(
    const std::unique_ptr<SourceElementSubtype>& element_ptr) {
    static_assert(
        std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
        "Template parameter type is not derived from SourceElement");
    return to_string_view(element_ptr.get());
}

// Convert the SourceElement to a std::string, using the method described above
// for StringView.
static std::string to_string(const fidl::raw::SourceElement& element) {
    return to_string_view(element);
}

static std::string to_string(const fidl::raw::SourceElement* element) {
    return to_string_view(*element);
}

template <typename SourceElementSubtype>
std::string to_string(
    const std::unique_ptr<SourceElementSubtype>& element_ptr) {
    static_assert(
        std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
        "Template parameter type is not derived from SourceElement");
    return to_string(element_ptr.get());
}

// Add a finding with |Finding| constructor arguments.
// This function is const because the Findings (TreeVisitor) object
// is not modified. It's Findings object (not owned) is updated.
template <typename... Args>
Finding& Linter::AddFinding(Args&&... args) const {
    assert(current_findings_ != nullptr);
    return current_findings_->emplace_back(std::forward<Args>(args)...);
}

// Add a finding with optional suggestion and replacement
template <typename SourceElementSubtypeRefOrPtr>
const Finding& Linter::AddFinding(
    const SourceElementSubtypeRefOrPtr& element,
    const CheckDef& check,
    Substitutions substitutions,
    std::string suggestion_template,
    std::string replacement_template) const {
    auto& finding = AddFinding(
        GetElementAsRef(element).location(),
        check.id(), check.message_template().Substitute(substitutions));
    if (suggestion_template.size() > 0) {
        if (replacement_template.size() == 0) {
            finding.SetSuggestion(
                TemplateString(suggestion_template).Substitute(substitutions));
        } else {
            finding.SetSuggestion(
                TemplateString(suggestion_template).Substitute(substitutions),
                TemplateString(replacement_template).Substitute(substitutions));
        }
    }
    return finding;
}

// Add a finding for an invalid identifier, and suggested replacement
template <typename SourceElementSubtypeRefOrPtr>
const Finding& Linter::AddReplaceIdFinding(
    const SourceElementSubtypeRefOrPtr& element,
    const CheckDef& check,
    std::string id,
    std::string replacement) const {
    return AddFinding(
        element,
        check,
        {
            {"IDENTIFIER", id},
            {"REPLACEMENT", replacement},
        },
        "change '${IDENTIFIER}' to '${REPLACEMENT}'",
        "${REPLACEMENT}");
}

const CheckDef& Linter::DefineCheck(std::string check_id,
                                    std::string message_template) {
    checks_.emplace_back(check_id, TemplateString(message_template));
    return checks_.back();
}

// Returns true if no new findings were generated
bool Linter::Lint(std::unique_ptr<raw::File> const& parsed_source,
                  Findings* findings) {
    size_t initial_findings_count = findings->size();
    current_findings_ = findings;
    callbacks_.Visit(parsed_source);
    current_findings_ = nullptr;
    if (findings->size() == initial_findings_count) {
        return true;
    }
    return false;
}

Linter::Linter()
    : callbacks_(LintingTreeCallbacks()) {

    callbacks_.OnUsing(
        [& linter = *this,
         check = DefineCheck(
             "invalid-case-for-primitive-alias",
             "Primitive aliases must be named in lower_snake_case")]
        //
        (const raw::Using& element) {
            if (element.maybe_alias != nullptr) {
                CHECK_CASE(lower_snake, element.maybe_alias)
            }
        });

    callbacks_.OnConstDeclaration(
        [& linter = *this,
         check = DefineCheck(
             "invalid-case-for-constant",
             "Constants must be named in ALL_CAPS_SNAKE_CASE")]
        //
        (const raw::ConstDeclaration& element) {
            CHECK_CASE(upper_snake, element.identifier)
        });

    callbacks_.OnEnumMember(
        [& linter = *this,
         check = DefineCheck(
             "invalid-case-for-enum-member",
             "Enum members must be named in ALL_CAPS_SNAKE_CASE")]
        //
        (const raw::EnumMember& element) {
            CHECK_CASE(upper_snake, element.identifier)
        });

    callbacks_.OnInterfaceDeclaration(
        [& linter = *this,
         check = DefineCheck(
             "invalid-case-for-protocol",
             "Protocols must be named in UpperCamelCase")]
        //
        (const raw::InterfaceDeclaration& element) {
            CHECK_CASE(upper_camel, element.identifier)
        });

    callbacks_.OnFile(
        [& linter = *this,
         check = DefineCheck(
             "disallowed-library-name-component",
             "Library names must not contain the following components: common, service, util, base, f<letter>l, zx<word>")]
        //
        (const raw::File& element) {
            static const std::regex disallowed_library_component(
                R"(^(common|service|util|base|f[a-z]l|zx\w*)$)");
            for (const auto& component : element.library_name->components) {
                if (std::regex_match(to_string(component),
                                     disallowed_library_component)) {
                    linter.AddFinding(component, check);
                    break;
                }
            }
        });

    callbacks_.OnFile(
        [& linter = *this,
         check = DefineCheck(
             "wrong-prefix-for-platform-source-library",
             "FIDL library name is not currently allowed")]
        //
        (const raw::File& element) {
            auto& prefix_component =
                element.library_name->components.front();
            std::string prefix = to_string(prefix_component);
            if (linter.permitted_library_prefixes_.find(prefix) ==
                linter.permitted_library_prefixes_.end()) {
                // TODO(fxb/FIDL-547): Implement more specific test,
                // comparing proposed library prefix to actual
                // source path.
                std::string replacement = "fuchsia, perhaps?";
                linter.AddFinding(
                    element.library_name, check,
                    {
                        {"ORIGINAL", prefix},
                        {"REPLACEMENT", replacement},
                    },
                    "change '${ORIGINAL}' to ${REPLACEMENT}",
                    "${REPLACEMENT}");
            }
        });

    auto& invalid_case_for_decl_member = DefineCheck(
        "invalid-case-for-decl-member",
        "Structs, unions, and tables members must be named in lower_snake_case");

    callbacks_.OnStructMember(
        [& linter = *this,
         check = invalid_case_for_decl_member]
        //
        (const raw::StructMember& element) {
            CHECK_CASE(lower_snake, element.identifier)
        });
    callbacks_.OnUnionMember(
        [& linter = *this,
         check = invalid_case_for_decl_member]
        //
        (const raw::UnionMember& element) {
            CHECK_CASE(lower_snake, element.identifier)
        });
    callbacks_.OnXUnionMember(
        [& linter = *this,
         check = invalid_case_for_decl_member]
        //
        (const raw::XUnionMember& element) {
            CHECK_CASE(lower_snake, element.identifier)
        });
    callbacks_.OnTableMember(
        [& linter = *this,
         check = invalid_case_for_decl_member]
        //
        (const raw::TableMember& element) {
            if (element.maybe_used != nullptr) {
                CHECK_CASE(lower_snake, element.maybe_used->identifier)
            }
        });
}

} // namespace linter
} // namespace fidl
