// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <utility>

#include <fidl/findings.h>
#include <fidl/linter.h>
#include <fidl/names.h>
#include <fidl/raw_ast.h>
#include <fidl/utils.h>

namespace fidl::linter {

namespace {

// Convert the SourceElement (start- and end-tokens within the SourceFile)
// to a std::string_view, spanning from the beginning of the start token, to the end
// of the end token. The three methods support classes derived from
// SourceElement, by reference, pointer, or unique_ptr.
std::string_view to_string_view(const fidl::raw::SourceElement& element) {
  return element.span().data();
}

template <typename SourceElementSubtype>
std::string_view to_string_view(const std::unique_ptr<SourceElementSubtype>& element_ptr) {
  static_assert(std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
                "Template parameter type is not derived from SourceElement");
  return to_string_view(element_ptr.get());
}

// Convert the SourceElement to a std::string, using the method described above
// for std::string_view.
std::string to_string(const fidl::raw::SourceElement& element) {
  return std::string(to_string_view(element));
}

template <typename SourceElementSubtype>
std::string to_string(const std::unique_ptr<SourceElementSubtype>& element_ptr) {
  static_assert(std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
                "Template parameter type is not derived from SourceElement");
  return to_string(*element_ptr);
}

}  // namespace

std::string Linter::MakeCopyrightBlock() {
  std::string copyright_block;
  for (const auto& line : kCopyrightLines) {
    copyright_block.append("\n");
    copyright_block.append(line);
  }
  return copyright_block;
}

const std::set<std::string>& Linter::permitted_library_prefixes() const {
  return kPermittedLibraryPrefixes;
}

std::string Linter::kPermittedLibraryPrefixesas_string() const {
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

static const fidl::raw::SourceElement& GetElementAsRef(const fidl::raw::SourceElement* element) {
  return GetElementAsRef(*element);
}

// Returns the pointed-to element as a reference.
template <typename SourceElementSubtype>
const fidl::raw::SourceElement& GetElementAsRef(
    const std::unique_ptr<SourceElementSubtype>& element_ptr) {
  static_assert(std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
                "Template parameter type is not derived from SourceElement");
  return GetElementAsRef(element_ptr.get());
}
// Add a finding with |Finding| constructor arguments.
// This function is const because the Findings (TreeVisitor) object
// is not modified. It's Findings object (not owned) is updated.
Finding* Linter::AddFinding(SourceSpan span, std::string check_id, std::string message) {
  auto result =
      current_findings_.emplace(new Finding(span, std::move(check_id), std::move(message)));
  // Future checks may need to allow multiple findings of the
  // same check ID at the same location.
  assert(result.second && "Duplicate key. Check criteria in Finding.operator==() and operator<()");
  return result.first->get();
}

// Add a finding with optional suggestion and replacement
const Finding* Linter::AddFinding(SourceSpan span, const CheckDef& check,
                                  const Substitutions& substitutions,
                                  const std::string& suggestion_template,
                                  const std::string& replacement_template) {
  auto* finding =
      AddFinding(span, std::string(check.id()), check.message_template().Substitute(substitutions));
  if (finding == nullptr) {
    return nullptr;
  }
  if (!suggestion_template.empty()) {
    if (replacement_template.empty()) {
      finding->SetSuggestion(TemplateString(suggestion_template).Substitute(substitutions));
    } else {
      finding->SetSuggestion(TemplateString(suggestion_template).Substitute(substitutions),
                             TemplateString(replacement_template).Substitute(substitutions));
    }
  }
  return finding;
}

// Add a finding from a SourceElement
template <typename SourceElementSubtypeRefOrPtr>
const Finding* Linter::AddFinding(const SourceElementSubtypeRefOrPtr& element,
                                  const CheckDef& check, Substitutions substitutions,
                                  std::string suggestion_template,
                                  std::string replacement_template) {
  return AddFinding(GetElementAsRef(element).span(), check, substitutions, suggestion_template,
                    replacement_template);
}

CheckDef Linter::DefineCheck(std::string_view check_id, std::string message_template) {
  auto result = checks_.emplace(check_id, TemplateString(std::move(message_template)));
  assert(result.second && "DefineCheck called with a duplicate check_id");
  return *result.first;
}

// Returns true if no new findings were generated
bool Linter::Lint(std::unique_ptr<raw::File> const& parsed_source, Findings* findings,
                  std::set<std::string>* excluded_checks_not_found) {
  auto initial_findings_size = findings->size();
  callbacks_.Visit(parsed_source);
  for (auto& finding_ptr : current_findings_) {
    auto check_id = finding_ptr->subcategory();
    if (excluded_checks_not_found && !excluded_checks_not_found->empty()) {
      excluded_checks_not_found->erase(check_id);
    }
    bool is_included = included_check_ids_.find(check_id) != included_check_ids_.end();
    bool is_excluded =
        exclude_by_default_ || excluded_check_ids_.find(check_id) != excluded_check_ids_.end();
    if (!is_excluded || is_included) {
      findings->emplace_back(std::move(*finding_ptr));
    }
  }
  current_findings_.clear();
  return findings->size() == initial_findings_size;
}

void Linter::NewFile(const raw::File& element) {
  // Reset file state variables (for a new file)
  line_comments_checked_ = 0;
  added_invalid_copyright_finding_ = false;
  good_copyright_lines_found_ = 0;
  copyright_date_ = "";

  auto& prefix_component = element.library_decl->path->components.front();
  library_prefix_ = to_string(prefix_component);

  library_is_platform_source_library_ =
      (kPermittedLibraryPrefixes.find(library_prefix_) != kPermittedLibraryPrefixes.end());

  filename_ = element.span().source_file().filename();

  file_is_in_platform_source_tree_ = false;

  auto in_fuchsia_dir_regex = std::regex(R"REGEX(\bfuchsia/)REGEX");
  if (std::regex_search(filename_, in_fuchsia_dir_regex)) {
    file_is_in_platform_source_tree_ = true;
  } else {
    file_is_in_platform_source_tree_ = std::ifstream(filename_.c_str()).good();
  }

  if (library_prefix_ == "zx") {
    lint_style_ = LintStyle::CStyle;
    invalid_case_for_decl_name_ =
        DefineCheck("invalid-case-for-decl-name", "${TYPE} must be named in lower_snake_case");
  } else {
    lint_style_ = LintStyle::IpcStyle;
    invalid_case_for_decl_name_ =
        DefineCheck("invalid-case-for-decl-name", "${TYPE} must be named in UpperCamelCase");
  }

  if (lint_style_ == LintStyle::IpcStyle && !library_is_platform_source_library_) {
    // TODO(fxbug.dev/7871): Implement more specific test,
    // comparing proposed library prefix to actual
    // source path.
    std::string replacement = "fuchsia, perhaps?";
    AddFinding(element.library_decl->path, kLibraryPrefixCheck,
               {
                   {"ORIGINAL", library_prefix_},
                   {"REPLACEMENT", replacement},
               },
               "change '${ORIGINAL}' to ${REPLACEMENT}", "${REPLACEMENT}");
  }

  // Library names should not have more than four components.
  if (element.library_decl->path->components.size() > 4) {
    AddFinding(element.library_decl->path, kLibraryNameDepthCheck);
  }

  // Library name is not checked for CStyle because it must be simply "zx".
  if (lint_style_ == LintStyle::IpcStyle) {
    for (const auto& component : element.library_decl->path->components) {
      if (std::regex_match(to_string(component), kDisallowedLibraryComponentRegex)) {
        AddFinding(component, kLibraryNameComponentCheck);
        break;
      }
    }
  }
  EnterContext("library");
}

const Finding* Linter::CheckCase(const std::string& type,
                                 const std::unique_ptr<raw::Identifier>& identifier,
                                 const CheckDef& check_def, const CaseType& case_type) {
  std::string id = to_string(identifier);
  if (!case_type.matches(id)) {
    return AddFinding(identifier, check_def,
                      {
                          {"TYPE", type},
                          {"IDENTIFIER", id},
                          {"REPLACEMENT", case_type.convert(id)},
                      },
                      "change '${IDENTIFIER}' to '${REPLACEMENT}'", "${REPLACEMENT}");
  }
  return nullptr;
}

std::string Linter::GetCopyrightSuggestion() {
  auto copyright_block = kCopyrightBlock;
  if (!copyright_date_.empty()) {
    copyright_block = TemplateString(copyright_block).Substitute({{"YYYY", copyright_date_}});
  }
  if (good_copyright_lines_found_ == 0) {
    return "Insert missing header:\n" + copyright_block;
  }
  return "Update your header with:\n" + copyright_block;
}

void Linter::AddInvalidCopyrightFinding(SourceSpan span) {
  if (!added_invalid_copyright_finding_) {
    added_invalid_copyright_finding_ = true;
    AddFinding(span, kInvalidCopyrightCheck, {}, GetCopyrightSuggestion());
  }
}

void Linter::CheckInvalidCopyright(SourceSpan span, std::string line_comment,
                                   std::string line_to_match) {
  if (line_comment == line_to_match ||
      // TODO(66908): Remove this branch once all platform FIDL files are updated.
      line_comment == line_to_match + " All rights reserved.") {
    good_copyright_lines_found_++;
    return;
  }
  if (CopyrightCheckIsComplete()) {
    return;
  }
  auto end_it = line_comment.end();
  if (line_comment.size() > line_to_match.size()) {
    end_it = line_comment.begin() + line_to_match.size();
  }
  auto first_mismatch = std::mismatch(line_comment.begin(), end_it, line_to_match.begin());
  auto index = first_mismatch.first - line_comment.begin();
  if (index > 0) {
    std::string_view error_view = span.data();
    error_view.remove_prefix(index);
    auto& source_file = span.source_file();
    span = SourceSpan(error_view, source_file);
  }
  AddInvalidCopyrightFinding(span);
}

bool Linter::CopyrightCheckIsComplete() {
  return !file_is_in_platform_source_tree_ || added_invalid_copyright_finding_ ||
         good_copyright_lines_found_ >= kCopyrightLines.size();
}

void Linter::ExitContext() { type_stack_.pop(); }

Linter::Linter()
    : kLibraryNameDepthCheck(DefineCheck("too-many-nested-libraries",
                                         "Avoid library names with more than three dots")),
      kLibraryNameComponentCheck(
          DefineCheck("disallowed-library-name-component",
                      "Library names must not contain the following components: common, service, "
                      "util, base, f<letter>l, zx<word>")),
      kLibraryPrefixCheck(DefineCheck("wrong-prefix-for-platform-source-library",
                                      "FIDL library name is not currently allowed")),
      kInvalidCopyrightCheck(
          DefineCheck("invalid-copyright-for-platform-source-library",
                      "FIDL files defined in the Platform Source Tree (i.e., defined in "
                      "fuchsia.googlesource.com) must begin with the standard copyright notice")),
      kCopyrightLines({
          // First line may also contain " All rights reserved."
          "// Copyright ${YYYY} The Fuchsia Authors.",
          "// Use of this source code is governed by a BSD-style license that can be",
          "// found in the LICENSE file.",
      }),
      kCopyrightBlock(MakeCopyrightBlock()),
      kDocAttribute("doc"),
      kYearRegex(R"(\b(\d{4})\b)"),
      kDisallowedLibraryComponentRegex(R"(^(common|service|util|base|f[a-z]l|zx\w*)$)"),
      kPermittedLibraryPrefixes({
          "fuchsia",
          "fidl",
          "test",
      }),
      kStopWords({
          // clang-format off
          "a",
          "about",
          "above",
          "after",
          "against",
          "all",
          "an",
          "and",
          "any",
          "are",
          "as",
          "at",
          "be",
          "because",
          "been",
          "before",
          "being",
          "below",
          "between",
          "both",
          "but",
          "by",
          "can",
          "did",
          "do",
          "does",
          "doing",
          "down",
          "during",
          "each",
          "few",
          "for",
          "from",
          "further",
          "had",
          "has",
          "have",
          "having",
          "here",
          "how",
          "if",
          "in",
          "into",
          "is",
          "it",
          "its",
          "itself",
          "just",
          "more",
          "most",
          "no",
          "nor",
          "not",
          "now",
          "of",
          "off",
          "on",
          "once",
          "only",
          "or",
          "other",
          "out",
          "over",
          "own",
          "same",
          "should",
          "so",
          "some",
          "such",
          "than",
          "that",
          "the",
          "then",
          "there",
          "these",
          "this",
          "those",
          "through",
          "to",
          "too",
          "under",
          "until",
          "up",
          "very",
          "was",
          "were",
          "what",
          "when",
          "where",
          "which",
          "while",
          "why",
          "will",
          "with",
          // clang-format on
      }) {
  auto copyright_should_not_be_doc_comment =
      DefineCheck("copyright-should-not-be-doc-comment",
                  "Copyright notice should use non-flow-through comment markers");
  auto explict_flexible_modifier = DefineCheck("explicit-flexible-modifier",
                                               "${TYPE} must have an explicit 'flexible' modifier");
  auto invalid_case_for_constant =
      DefineCheck("invalid-case-for-constant", "${TYPE} must be named in ALL_CAPS_SNAKE_CASE");
  auto invalid_case_for_decl_member =
      DefineCheck("invalid-case-for-decl-member", "${TYPE} must be named in lower_snake_case");
  auto modifiers_order = DefineCheck(
      "modifier-order", "Strictness modifier on ${TYPE} must always precede the resource modifier");
  auto todo_should_not_be_doc_comment =
      DefineCheck("todo-should-not-be-doc-comment",
                  "TODO comment should use a non-flow-through comment marker");
  auto string_bounds_not_specified =
      DefineCheck("string-bounds-not-specified", "Specify bounds for string");
  auto vector_bounds_not_specified =
      DefineCheck("vector-bounds-not-specified", "Specify bounds for vector");

  // clang-format off
  callbacks_.OnFile(
    [& linter = *this]
    //
    (const raw::File& element) {
      linter.NewFile(element);
    });
  // clang-format on

  callbacks_.OnLineComment(
      [&linter = *this]
      //
      (const SourceSpan& span, std::string_view line_prefix_view) {
        linter.line_comments_checked_++;
        if (linter.CopyrightCheckIsComplete() &&
            linter.line_comments_checked_ > linter.kCopyrightLines.size()) {
          return;
        }
        // span.position() is not a lightweight operation, but as long as
        // the conditions above are checked first, the line number only needs
        // to be computed a minimum number of times.
        size_t line_number = span.position().line;
        std::string line_comment = std::string(span.data());
        if (line_number > linter.kCopyrightLines.size()) {
          if (!linter.CopyrightCheckIsComplete()) {
            linter.AddInvalidCopyrightFinding(span);
          }
          return;
        }
        if (linter.copyright_date_.empty()) {
          std::smatch match_year;
          if (std::regex_search(line_comment, match_year, linter.kYearRegex)) {
            linter.copyright_date_ = match_year[1];
          }
        }
        auto line_to_match = linter.kCopyrightLines[line_number - 1];
        if (!linter.copyright_date_.empty()) {
          line_to_match =
              TemplateString(line_to_match).Substitute({{"YYYY", linter.copyright_date_}});
        }
        linter.CheckInvalidCopyright(span, line_comment, line_to_match);
      });

  callbacks_.OnExitFile([&linter = *this]
                        //
                        (const raw::File& element) {
                          if (!linter.CopyrightCheckIsComplete()) {
                            auto& source_file = element.span().source_file();
                            std::string_view error_view = source_file.data();
                            error_view.remove_suffix(source_file.data().size());
                            linter.AddInvalidCopyrightFinding(SourceSpan(error_view, source_file));
                          }
                          linter.ExitContext();
                        });

  // TODO(fxbug.dev/7978): Remove this check after issues are resolved with
  // trailing comments in existing source and tools
  // clang-format off
  callbacks_.OnLineComment(
      [& linter = *this,
       trailing_comment_check = DefineCheck("no-trailing-comment",
                                            "Place comments above the thing being described")]
      //
      (const SourceSpan& span, std::string_view line_prefix_view) {
        if (!utils::IsBlank(line_prefix_view)) {
          linter.AddFinding(span, trailing_comment_check);
        }
      });
  // clang-format on

  callbacks_.OnUsing([&linter = *this,
                      case_check = DefineCheck("invalid-case-for-using-alias",
                                               "Using aliases must be named in lower_snake_case"),
                      &case_type = lower_snake_]
                     //
                     (const raw::Using& element) {
                       if (element.maybe_alias != nullptr) {
                         linter.CheckCase("using alias", element.maybe_alias, case_check,
                                          case_type);
                       }
                     });

  callbacks_.OnConstDeclaration(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::ConstDeclaration& element) {
        linter.CheckCase("constants", element.identifier, case_check, case_type);
        linter.in_const_declaration_ = true;
      });

  callbacks_.OnExitConstDeclaration(
      [&linter = *this]
      //
      (const raw::ConstDeclaration& element) { linter.in_const_declaration_ = false; });

  callbacks_.OnProtocolDeclaration(
      [&linter = *this,
       name_contains_service_check = DefineCheck("protocol-name-includes-service",
                                                 "Protocols must not include the name 'service.'")]
      //
      (const raw::ProtocolDeclaration& element) {
        linter.CheckCase("protocols", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        for (const auto& word : utils::id_to_words(to_string(element.identifier))) {
          if (word == "service") {
            linter.AddFinding(element.identifier, name_contains_service_check);
            break;
          }
        }
        linter.EnterContext("protocol");
      });
  callbacks_.OnMethod([&linter = *this]
                      //
                      (const raw::ProtocolMethod& element) {
                        linter.CheckCase("methods", element.identifier,
                                         linter.invalid_case_for_decl_name(),
                                         linter.decl_case_type_for_style());
                      });
  callbacks_.OnEvent(
      [&linter = *this, event_check = DefineCheck("event-names-must-start-with-on",
                                                  "Event names must start with 'On'")]
      //
      (const raw::ProtocolMethod& element) {
        std::string id = to_string(element.identifier);
        auto finding =
            linter.CheckCase("events", element.identifier, linter.invalid_case_for_decl_name(),
                             linter.decl_case_type_for_style());
        if (finding && finding->suggestion().has_value()) {
          auto& suggestion = finding->suggestion().value();
          if (suggestion.replacement().has_value()) {
            id = suggestion.replacement().value();
          }
        }
        if ((id.compare(0, 2, "On") != 0) || !isupper(id[2])) {
          std::string replacement = "On" + id;
          linter.AddFinding(element.identifier, event_check,
                            {
                                {"IDENTIFIER", id},
                                {"REPLACEMENT", replacement},
                            },
                            "change '${IDENTIFIER}' to '${REPLACEMENT}'", "${REPLACEMENT}");
        }
      });
  callbacks_.OnParameter(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::Parameter& element) {
        linter.CheckCase("parameters", element.identifier, case_check, case_type);
      });
  callbacks_.OnExitProtocolDeclaration(
      [&linter = *this]
      //
      (const raw::ProtocolDeclaration& element) { linter.ExitContext(); });

  // TODO(fxbug.dev/70247): Delete this.
  // --- start old syntax ---
  callbacks_.OnAttributeOld(
      [&linter = *this, check = copyright_should_not_be_doc_comment,
       copyright_regex =
           std::regex(R"REGEX(^[ \t]*Copyright \d\d\d\d\W)REGEX", std::regex_constants::icase),
       todo_check = todo_should_not_be_doc_comment,
       todo_regex = std::regex(R"REGEX(^[ \t]*TODO\W)REGEX")]
      //
      (const raw::AttributeOld& element) {
        if (utils::to_lower_snake_case(element.name) == linter.kDocAttribute) {
          auto doc_comment = static_cast<raw::DocCommentLiteral*>(element.value.get());
          if (std::regex_search(doc_comment->MakeContents(), copyright_regex)) {
            linter.AddFinding(element, check, {}, "change '///' to '//'", "//");
          }
          if (std::regex_search(doc_comment->MakeContents(), todo_regex)) {
            linter.AddFinding(element, todo_check, {}, "change '///' to '//'", "//");
          }
        }
      });

  callbacks_.OnBitsDeclaration([&linter = *this]
                               //
                               (const raw::BitsDeclaration& element) {
                                 linter.CheckCase("bitfields", element.identifier,
                                                  linter.invalid_case_for_decl_name(),
                                                  linter.decl_case_type_for_style());
                                 linter.EnterContext("bitfield");
                               });
  callbacks_.OnBitsMember(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::BitsMember& element) {
        linter.CheckCase("bitfield members", element.identifier, case_check, case_type);
      });
  callbacks_.OnExitBitsDeclaration([&linter = *this]
                                   //
                                   (const raw::BitsDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnEnumDeclaration([&linter = *this]
                               //
                               (const raw::EnumDeclaration& element) {
                                 linter.CheckCase("enums", element.identifier,
                                                  linter.invalid_case_for_decl_name(),
                                                  linter.decl_case_type_for_style());
                                 linter.EnterContext("enum");
                               });
  callbacks_.OnEnumMember(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::EnumMember& element) {
        linter.CheckCase("enum members", element.identifier, case_check, case_type);
      });
  callbacks_.OnExitEnumDeclaration([&linter = *this]
                                   //
                                   (const raw::EnumDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnStructDeclaration([&linter = *this]
                                 //
                                 (const raw::StructDeclaration& element) {
                                   linter.CheckCase("structs", element.identifier,
                                                    linter.invalid_case_for_decl_name(),
                                                    linter.decl_case_type_for_style());
                                   linter.EnterContext("struct");
                                 });
  callbacks_.OnStructMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::StructMember& element) {
        linter.CheckCase("struct members", element.identifier, case_check, case_type);
      });
  callbacks_.OnExitStructDeclaration(
      [&linter = *this]
      //
      (const raw::StructDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnTableDeclaration([&linter = *this]
                                //
                                (const raw::TableDeclaration& element) {
                                  linter.CheckCase("tables", element.identifier,
                                                   linter.invalid_case_for_decl_name(),
                                                   linter.decl_case_type_for_style());
                                  linter.EnterContext("table");
                                });
  callbacks_.OnTableMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::TableMember& element) {
        if (element.maybe_used == nullptr)
          return;
        linter.CheckCase("table members", element.maybe_used->identifier, case_check, case_type);
      });
  callbacks_.OnExitTableDeclaration(
      [&linter = *this]
      //
      (const raw::TableDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnUnionDeclaration([&linter = *this]
                                //
                                (const raw::UnionDeclaration& element) {
                                  linter.CheckCase("unions", element.identifier,
                                                   linter.invalid_case_for_decl_name(),
                                                   linter.decl_case_type_for_style());
                                  linter.EnterContext("union");
                                });
  callbacks_.OnUnionMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::UnionMember& element) {
        if (element.maybe_used == nullptr)
          return;
        linter.CheckCase("union members", element.maybe_used->identifier, case_check, case_type);
      });
  callbacks_.OnExitUnionDeclaration(
      [&linter = *this]
      //
      (const raw::UnionDeclaration& element) { linter.ExitContext(); });

  // clang-format off
  callbacks_.OnTypeConstructorOld(
      [& linter = *this,
       string_bounds_check = string_bounds_not_specified,
       vector_bounds_check = vector_bounds_not_specified]
      //
      (const raw::TypeConstructorOld& element) {
        if (element.identifier->components.size() != 1) {
          return;
        }
        auto type = to_string(element.identifier->components[0]);
        if (!linter.in_const_declaration_) {
          if (type == "string" && element.maybe_size == nullptr) {
            linter.AddFinding(element.identifier, string_bounds_check);
          }
          if (type == "vector" && element.maybe_size == nullptr) {
            linter.AddFinding(element.identifier, vector_bounds_check);
          }
        }
      });
  // clang-format on
  // --- end old syntax ---

  // --- start new syntax ---
  callbacks_.OnAttributeNew(
      [&linter = *this, check = copyright_should_not_be_doc_comment,
       copyright_regex =
           std::regex(R"REGEX(^[ \t]*Copyright \d\d\d\d\W)REGEX", std::regex_constants::icase),
       todo_check = todo_should_not_be_doc_comment,
       todo_regex = std::regex(R"REGEX(^[ \t]*TODO\W)REGEX")]
      //
      (const raw::AttributeNew& element) {
        if (utils::to_lower_snake_case(element.name) == linter.kDocAttribute) {
          if (element.args.empty())
            return;
          auto doc_comment = static_cast<raw::DocCommentLiteral*>(element.args.front().value.get());
          if (std::regex_search(doc_comment->MakeContents(), copyright_regex)) {
            linter.AddFinding(element, check, {}, "change '///' to '//'", "//");
          }
          if (std::regex_search(doc_comment->MakeContents(), todo_regex)) {
            linter.AddFinding(element, todo_check, {}, "change '///' to '//'", "//");
          }
        }
      });
  callbacks_.OnTypeDecl(
      [&linter = *this]
      //
      (const raw::TypeDecl& element) {
        std::string context_type;
        const auto& layout =
            static_cast<raw::InlineLayoutReference*>(element.type_ctor->layout_ref.get())->layout;
        switch (layout->kind) {
          case raw::Layout::kBits: {
            context_type = "bitfield";
            break;
          }
          case raw::Layout::kEnum: {
            context_type = "enum";
            break;
          }
          case raw::Layout::kStruct: {
            context_type = "struct";
            break;
          }
          case raw::Layout::kTable: {
            context_type = "table";
            break;
          }
          case raw::Layout::kUnion: {
            context_type = "union";
            break;
          }
        }

        linter.CheckCase(context_type + "s", element.identifier,
                         linter.invalid_case_for_decl_name(), linter.decl_case_type_for_style());
        linter.EnterContext(context_type);
      });
  callbacks_.OnLayout(
      [&linter = *this, explict_flexible_modifier_check = explict_flexible_modifier,
       modifiers_order_check = modifiers_order]
      //
      (const raw::Layout& element) {
        std::string parent_type = linter.type_stack_.top();
        // The "parent_type" for type declarations is set in the OnTypeDecl callback.  This callback
        // is not invoked in the case of protocol request/response parameter lists, resulting a
        // "protocol" naming context instead.  However, since protocol request/response parameter
        // lists are always structs, we can just treat "protocol" as a "struct" for the purposes of
        // ths lint.
        if (parent_type == "protocol")
          parent_type = "struct";

        // All strictness-carrying declarations (bits, enums, unions) must specify the strictness
        // explicitly.
        if (parent_type != "table" && parent_type != "struct" &&
            (element.modifiers == nullptr || element.modifiers->maybe_strictness == std::nullopt)) {
          linter.AddFinding(element, explict_flexible_modifier_check,
                            {
                                {"TYPE", parent_type},
                            },
                            "add 'flexible' modifier before ${TYPE} keyword", "");
        }

        // Only union declarations can successfully parse with both modifiers attached.
        if ((parent_type == "bitfield" || parent_type == "enum" || parent_type == "union") &&
            element.modifiers != nullptr && element.modifiers->maybe_strictness != std::nullopt &&
            element.modifiers->resourceness_comes_first) {
          linter.AddFinding(
              element, modifiers_order_check,
              {
                  {"TYPE", parent_type},
                  {"STRICTNESS",
                   std::string(element.modifiers->maybe_strictness_token->span().data())},
              },
              "move '${STRICTNESS}' modifier before resource modifier for ${TYPE}", "");
        }
      });
  callbacks_.OnOrdinaledLayoutMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::OrdinaledLayoutMember& element) {
        if (element.reserved)
          return;
        std::string parent_type = linter.type_stack_.top();
        linter.CheckCase(parent_type + " members", element.identifier, case_check, case_type);
      });
  callbacks_.OnStructLayoutMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::StructLayoutMember& element) {
        std::string parent_type = linter.type_stack_.top();
        if (parent_type == "protocol") {
          linter.CheckCase("parameters", element.identifier, case_check, case_type);
          return;
        }

        linter.CheckCase("struct members", element.identifier, case_check, case_type);
      });
  callbacks_.OnValueLayoutMember(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::ValueLayoutMember& element) {
        std::string parent_type = linter.type_stack_.top();
        linter.CheckCase(parent_type + " members", element.identifier, case_check, case_type);
      });
  callbacks_.OnExitTypeDecl([&linter = *this]
                            //
                            (const raw::TypeDecl& element) { linter.ExitContext(); });

  // clang-format off
  callbacks_.OnIdentifierLayoutParameter(
      [& linter = *this,
          string_bounds_check = string_bounds_not_specified,
          vector_bounds_check = vector_bounds_not_specified]
          //
          (const raw::IdentifierLayoutParameter& element) {
        if (element.identifier->span().data() == "string") {
          linter.AddFinding(element.identifier, string_bounds_check);
        }
      });
  callbacks_.OnTypeConstructorNew(
      [& linter = *this,
          string_bounds_check = string_bounds_not_specified,
          vector_bounds_check = vector_bounds_not_specified]
          //
          (const raw::TypeConstructorNew& element) {
        if (element.layout_ref->kind != raw::LayoutReference::kNamed)
          return;
        const auto as_named = static_cast<raw::NamedLayoutReference*>(element.layout_ref.get());

        if (as_named->identifier->components.size() != 1) {
          return;
        }
        auto type = to_string((as_named->identifier->components[0]));
        if (!linter.in_const_declaration_) {
          // If there is a size attached to this type, it will always be the first numeric value in
          // the constraints list.
          bool has_size = false;
          if (element.constraints != nullptr && !element.constraints->items.empty()) {
            const auto& first_constraint = element.constraints->items.front();
            if (first_constraint->kind == raw::Constant::Kind::kLiteral) {
              const auto as_lit_const = static_cast<raw::LiteralConstant*>(first_constraint.get());
              if (as_lit_const->literal->kind == raw::Literal::Kind::kNumeric) {
                has_size = true;
              }
            } else if (first_constraint->kind == raw::Constant::Kind::kIdentifier && first_constraint->span().data() != "optional") {
              // TODO(fxbug.dev/77561): This check currently fails to recognize a shadowing const
              //  named optional, like:
              //
              //    const optional uint16 = 1234;
              //    type MyStruct = struct {
              //      this_will_trigger_incorrect_linter_warning string:optional;
              //    };
              has_size = true;
            }
          }

          if (type == "string" && !has_size) {
            linter.AddFinding(as_named->identifier, string_bounds_check);
          }
          if (type == "vector" && !has_size) {
            linter.AddFinding(as_named->identifier, vector_bounds_check);
          }
        }
      });
  // clang-format on
  // --- end new syntax ---
}

}  // namespace fidl::linter
