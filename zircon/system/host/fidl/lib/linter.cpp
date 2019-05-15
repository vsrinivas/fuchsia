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
// to a std::string_view, spanning from the beginning of the start token, to the end
// of the end token. The three methods support classes derived from
// SourceElement, by reference, pointer, or unique_ptr.
static std::string_view to_string_view(const fidl::raw::SourceElement& element) {
    auto start_string = element.start_.data();
    const char* start_ptr = start_string.data();
    auto end_string = element.end_.data();
    const char* end_ptr = end_string.data() + end_string.size();
    size_t size = static_cast<size_t>(end_ptr - start_ptr);
    return std::string_view(start_ptr, size);
}

static std::string_view to_string_view(const fidl::raw::SourceElement* element) {
    return to_string_view(*element);
}

template <typename SourceElementSubtype>
std::string_view to_string_view(
    const std::unique_ptr<SourceElementSubtype>& element_ptr) {
    static_assert(
        std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
        "Template parameter type is not derived from SourceElement");
    return to_string_view(element_ptr.get());
}

// Convert the SourceElement to a std::string, using the method described above
// for std::string_view.
static std::string to_string(const fidl::raw::SourceElement& element) {
    return std::string(to_string_view(element));
}

static std::string to_string(const fidl::raw::SourceElement* element) {
    return std::string(to_string_view(*element));
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

CheckDef Linter::DefineCheck(std::string check_id,
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

const Finding* Linter::CheckCase(std::string type,
                                 const std::unique_ptr<raw::Identifier>& identifier,
                                 const CheckDef& check_def, const CaseType& case_type) {
    std::string id = to_string(identifier);
    if (!case_type.matches(id)) {
        return &(AddFinding(
            identifier, check_def,
            {
                {"TYPE", type},
                {"IDENTIFIER", id},
                {"REPLACEMENT", case_type.convert(id)},
            },
            "change '${IDENTIFIER}' to '${REPLACEMENT}'",
            "${REPLACEMENT}"));
    }
    return nullptr;
}

const Finding* Linter::CheckRepeatedName(std::string type,
                                         const std::unique_ptr<raw::Identifier>& identifier) {
    std::string id = to_string(identifier);
    auto split_id = utils::id_to_words(id);
    std::set<std::string> words;
    words.insert(split_id.begin(), split_id.end());
    std::set<std::string> repeats;
    for (auto& context : context_stack_) {
        std::set_intersection(words.begin(), words.end(),
                              context.words().begin(), context.words().end(),
                              std::inserter(repeats, repeats.begin()));
        if (!repeats.empty()) {
            // TODO(fxb/FIDL-628): Modify check to allow repeated names if otherwise ambiguous.
            std::string repeated_names;
            for (const auto& repeat : repeats) {
                if (!repeated_names.empty()) {
                    repeated_names.append(", ");
                }
                repeated_names.append(repeat);
            }
            return &(AddFinding(
                identifier, context.context_check(),
                {
                    {"TYPE", type},
                    {"REPEATED_NAMES", repeated_names},
                    {"CONTEXT_TYPE", context.type()},
                    {"CONTEXT_ID", context.id()},
                }));
        }
    }
    return nullptr;
}

static std::string to_library_id(const std::vector<std::unique_ptr<raw::Identifier>>& components) {
    std::string id;
    for (const auto& component : components) {
        if (!id.empty()) {
            id.append(".");
        }
        id.append(to_string(component));
    }
    return id;
}

Linter::Linter()
    : callbacks_(LintingTreeCallbacks()) {

    callbacks_.OnUsing(
        [& linter = *this,
         case_check = DefineCheck(
             "invalid-case-for-primitive-alias",
             "Primitive aliases must be named in lower_snake_case"),
         &case_type = lower_snake_]
        //
        (const raw::Using& element) {
            if (element.maybe_alias != nullptr) {
                linter.CheckCase("primitive alias", element.maybe_alias,
                                 case_check, case_type);
                linter.CheckRepeatedName("primitive alias", element.maybe_alias);
            }
        });

    auto invalid_case_for_constant = DefineCheck(
        "invalid-case-for-constant",
        "${TYPE} must be named in ALL_CAPS_SNAKE_CASE");

    callbacks_.OnConstDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_constant,
         &case_type = upper_snake_]
        //
        (const raw::ConstDeclaration& element) {
            linter.CheckCase("constants", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("constant", element.identifier);
        });

    callbacks_.OnEnumMember(
        [& linter = *this,
         case_check = invalid_case_for_constant,
         &case_type = upper_snake_]
        //
        (const raw::EnumMember& element) {
            linter.CheckCase("enum members", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("enum member", element.identifier);
        });

    callbacks_.OnBitsMember(
        [& linter = *this,
         case_check = invalid_case_for_constant,
         &case_type = upper_snake_]
        //
        (const raw::BitsMember& element) {
            linter.CheckCase("bitfield members", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("bitfield member", element.identifier);
        });

    auto invalid_case_for_decl_name = DefineCheck(
        "invalid-case-for-decl-name",
        "${TYPE} must be named in UpperCamelCase");

    auto name_repeats_enclosing_type_name = DefineCheck(
        "name-repeats-enclosing-type-name",
        "${TYPE} names (${REPEATED_NAMES}) must not repeat names from the "
        "enclosing ${CONTEXT_TYPE} '${CONTEXT_ID}'");

    callbacks_.OnInterfaceDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::InterfaceDeclaration& element) {
            linter.CheckCase("protocols", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("protocol", element.identifier);
            linter.EnterContext("protocol", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitInterfaceDeclaration(
        [& linter = *this]
        //
        (const raw::InterfaceDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnMethod(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_]
        //
        (const raw::InterfaceMethod& element) {
            linter.CheckCase("methods", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("method", element.identifier);
        });

    callbacks_.OnEvent(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         event_check = DefineCheck("event-names-must-start-with-on",
                                   "Event names must start with 'On'"),
         &case_type = upper_camel_]
        //
        (const raw::InterfaceMethod& element) {
            std::string id = to_string(element.identifier);
            auto finding = linter.CheckCase("events", element.identifier,
                                            case_check, case_type);
            if (finding) {
                id = *finding->suggestion()->replacement();
            }
            if ((id.compare(0, 2, "On") != 0) || !isupper(id[2])) {
                std::string replacement = "On" + id;
                linter.AddFinding(
                    element.identifier, event_check,
                    {
                        {"IDENTIFIER", id},
                        {"REPLACEMENT", replacement},
                    },
                    "change '${IDENTIFIER}' to '${REPLACEMENT}'",
                    "${REPLACEMENT}");
            }
            linter.CheckRepeatedName("event", element.identifier);
        });

    callbacks_.OnEnumDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::EnumDeclaration& element) {
            linter.CheckCase("enums", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("enum", element.identifier);
            linter.EnterContext("enum", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitEnumDeclaration(
        [& linter = *this]
        //
        (const raw::EnumDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnBitsDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::BitsDeclaration& element) {
            linter.CheckCase("bitfields", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("bitfield", element.identifier);
            linter.EnterContext("bitfield", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitBitsDeclaration(
        [& linter = *this]
        //
        (const raw::BitsDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnStructDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::StructDeclaration& element) {
            linter.CheckCase("structs", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("struct", element.identifier);
            linter.EnterContext("struct", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitStructDeclaration(
        [& linter = *this]
        //
        (const raw::StructDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnTableDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::TableDeclaration& element) {
            linter.CheckCase("tables", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("table", element.identifier);
            linter.EnterContext("table", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitTableDeclaration(
        [& linter = *this]
        //
        (const raw::TableDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnUnionDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::UnionDeclaration& element) {
            linter.CheckCase("unions", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("union", element.identifier);
            linter.EnterContext("union", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitUnionDeclaration(
        [& linter = *this]
        //
        (const raw::UnionDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnXUnionDeclaration(
        [& linter = *this,
         case_check = invalid_case_for_decl_name,
         &case_type = upper_camel_,
         context_check = name_repeats_enclosing_type_name]
        //
        (const raw::XUnionDeclaration& element) {
            linter.CheckCase("xunions", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("xunion", element.identifier);
            linter.EnterContext("xunion", to_string(element.identifier), context_check);
        });

    callbacks_.OnExitXUnionDeclaration(
        [& linter = *this]
        //
        (const raw::XUnionDeclaration& element) {
            linter.ExitContext();
        });

    callbacks_.OnFile(
        [& linter = *this,
         check = DefineCheck(
             "disallowed-library-name-component",
             "Library names must not contain the following components: common, service, util, base, f<letter>l, zx<word>"),
         context_check = DefineCheck(
             "name-repeats-library-name",
             "${TYPE} names (${REPEATED_NAMES}) must not repeat names from the "
             "library '${CONTEXT_ID}'")]
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
            linter.EnterContext("library", to_library_id(element.library_name->components),
                                context_check);
        });

    callbacks_.OnExitFile(
        [& linter = *this]
        //
        (const raw::File& element) {
            linter.ExitContext();
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

    auto invalid_case_for_decl_member = DefineCheck(
        "invalid-case-for-decl-member",
        "${TYPE} must be named in lower_snake_case");

    callbacks_.OnParameter(
        [& linter = *this,
         case_check = invalid_case_for_decl_member,
         &case_type = lower_snake_]
        //
        (const raw::Parameter& element) {
            linter.CheckCase("parameters", element.identifier,
                             case_check, case_type);
        });
    callbacks_.OnStructMember(
        [& linter = *this,
         case_check = invalid_case_for_decl_member,
         &case_type = lower_snake_]
        //
        (const raw::StructMember& element) {
            linter.CheckCase("struct members", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("struct member", element.identifier);
        });
    callbacks_.OnTableMember(
        [& linter = *this,
         case_check = invalid_case_for_decl_member,
         &case_type = lower_snake_]
        //
        (const raw::TableMember& element) {
            if (element.maybe_used != nullptr) {
                linter.CheckCase("table members", element.maybe_used->identifier,
                                 case_check, case_type);
                linter.CheckRepeatedName("table member", element.maybe_used->identifier);
            }
        });
    callbacks_.OnUnionMember(
        [& linter = *this,
         case_check = invalid_case_for_decl_member,
         &case_type = lower_snake_]
        //
        (const raw::UnionMember& element) {
            linter.CheckCase("union members", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("union member", element.identifier);
        });
    callbacks_.OnXUnionMember(
        [& linter = *this,
         case_check = invalid_case_for_decl_member,
         &case_type = lower_snake_]
        //
        (const raw::XUnionMember& element) {
            linter.CheckCase("xunion members", element.identifier,
                             case_check, case_type);
            linter.CheckRepeatedName("xunion member", element.identifier);
        });
} // namespace linter

} // namespace linter
} // namespace fidl
