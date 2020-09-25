// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

#include <fidl/findings.h>
#include <fidl/linter.h>
#include <fidl/names.h>
#include <fidl/raw_ast.h>
#include <fidl/utils.h>

namespace fidl {
namespace linter {

namespace {

// Convert the SourceElement (start- and end-tokens within the SourceFile)
// to a std::string_view, spanning from the beginning of the start token, to the end
// of the end token. The three methods support classes derived from
// SourceElement, by reference, pointer, or unique_ptr.
static std::string_view to_string_view(const fidl::raw::SourceElement& element) {
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
static std::string to_string(const fidl::raw::SourceElement& element) {
  return std::string(to_string_view(element));
}

template <typename SourceElementSubtype>
std::string to_string(const std::unique_ptr<SourceElementSubtype>& element_ptr) {
  static_assert(std::is_base_of<fidl::raw::SourceElement, SourceElementSubtype>::value,
                "Template parameter type is not derived from SourceElement");
  return to_string(*element_ptr.get());
}

}  // namespace

std::string Linter::MakeCopyrightBlock() {
  std::string copyright_block;
  for (auto line : kCopyrightLines) {
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
  auto result = current_findings_.emplace(new Finding(span, check_id, message));
  // Future checks may need to allow multiple findings of the
  // same check ID at the same location.
  assert(result.second && "Duplicate key. Check criteria in Finding.operator==() and operator<()");
  return result.first->get();
}

// Add a finding with optional suggestion and replacement
const Finding* Linter::AddFinding(SourceSpan span, const CheckDef& check,
                                  Substitutions substitutions, std::string suggestion_template,
                                  std::string replacement_template) {
  auto* finding = AddFinding(span, check.id(), check.message_template().Substitute(substitutions));
  if (finding == nullptr) {
    return nullptr;
  }
  if (suggestion_template.size() > 0) {
    if (replacement_template.size() == 0) {
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

CheckDef Linter::DefineCheck(std::string check_id, std::string message_template) {
  auto result = checks_.emplace(check_id, TemplateString(message_template));
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

  auto& prefix_component = element.library_name->components.front();
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
    // TODO(fxbug.dev/FIDL-547): Implement more specific test,
    // comparing proposed library prefix to actual
    // source path.
    std::string replacement = "fuchsia, perhaps?";
    AddFinding(element.library_name, kLibraryPrefixCheck,
               {
                   {"ORIGINAL", library_prefix_},
                   {"REPLACEMENT", replacement},
               },
               "change '${ORIGINAL}' to ${REPLACEMENT}", "${REPLACEMENT}");
  }

  // Library names should not have more than three components, except for
  // fuchsia.hardware.* libraries, where we allow four library components.
  bool libraryNameTooDeep = false;
  if (element.library_name->components.size() > 3) {
    if (element.library_name->components.at(0)->span().data() == "fuchsia" &&
        element.library_name->components.at(1)->span().data() == "hardware") {
      if (element.library_name->components.size() > 4) {
        libraryNameTooDeep = true;
      }
    } else {
      libraryNameTooDeep = true;
    }
  }
  if (libraryNameTooDeep) {
    AddFinding(element.library_name, kLibraryNameDepthCheck);
  }

  // Library name is not checked for CStyle because it must be simply "zx".
  if (lint_style_ == LintStyle::IpcStyle) {
    for (const auto& component : element.library_name->components) {
      if (std::regex_match(to_string(component), kDisallowedLibraryComponentRegex)) {
        AddFinding(component, kLibraryNameComponentCheck);
        break;
      }
    }
  }
  EnterContext("library", NameLibrary(element.library_name->components), kRepeatsLibraryNameCheck);
}

const Finding* Linter::CheckCase(std::string type,
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

void Linter::CheckRepeatedName(std::string type,
                               const std::unique_ptr<raw::Identifier>& identifier) {
  std::string id = to_string(identifier);
  auto split_id = utils::id_to_words(id, kStopWords);
  std::set<std::string> words;
  words.insert(split_id.begin(), split_id.end());
  for (auto& context : context_stack_) {
    std::set<std::string> repeats;
    std::set_intersection(words.begin(), words.end(), context.words().begin(),
                          context.words().end(), std::inserter(repeats, repeats.begin()));
    if (!repeats.empty()) {
      context.AddRepeatsContextNames(type, identifier->span(), repeats);
    }
  }
}

const Finding* Linter::AddRepeatedNameFinding(const Context& context,
                                              const Context::RepeatsContextNames& name_repeater) {
  std::string repeated_names;
  for (const auto& repeat : name_repeater.repeats) {
    if (!repeated_names.empty()) {
      repeated_names.append(", ");
    }
    repeated_names.append(repeat);
  }
  return AddFinding(name_repeater.span, context.context_check(),
                    {
                        {"TYPE", name_repeater.type},
                        {"REPEATED_NAMES", repeated_names},
                        {"CONTEXT_TYPE", context.type()},
                        {"CONTEXT_ID", context.id()},
                    });
}

std::string Linter::GetCopyrightSuggestion() {
  auto copyright_block = kCopyrightBlock;
  if (!copyright_date_.empty()) {
    copyright_block = TemplateString(copyright_block).Substitute({{"YYYY", copyright_date_}});
  }
  if (good_copyright_lines_found_ == 0) {
    return "Insert missing header:\n" + copyright_block;
  } else {
    return "Update your header with:\n" + copyright_block;
  }
}

void Linter::AddInvalidCopyrightFinding(SourceSpan span) {
  if (!added_invalid_copyright_finding_) {
    added_invalid_copyright_finding_ = true;
    AddFinding(span, kInvalidCopyrightCheck, {}, GetCopyrightSuggestion());
  }
}

void Linter::CheckInvalidCopyright(SourceSpan span, std::string line_comment,
                                   std::string line_to_match) {
  if (line_comment == line_to_match) {
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

void Linter::ExitContext() {
  Context context = std::move(context_stack_.front());
  context_stack_.pop_front();

  // Check the |RepeatsContextNames| objects in context.name_repeaters(), and
  // produce Finding objects for any identifier that is not allowed to repeat
  // a name from this |Context|.
  //
  // This check addresses the FIDL Rubric rule:
  //
  //     Member names must not repeat names from the enclosing type (or
  //     library) unless the member name is ambiguous without a name from
  //     the enclosing type...
  //
  //     ...a type DeviceToRoom--that associates a smart device with the room
  //     it's located in--may need to have members device_id and room_name,
  //     because id and name are ambiguous; they could refer to either the
  //     device or the room.
  auto& repeaters = context.name_repeaters();
  for (size_t i = 1; i < repeaters.size(); i++) {
    std::set<std::string> diffs;
    std::set_difference(repeaters[i - 1].repeats.begin(), repeaters[i - 1].repeats.end(),
                        repeaters[i].repeats.begin(), repeaters[i].repeats.end(),
                        std::inserter(diffs, diffs.begin()));
    if (!diffs.empty()) {
      // If there are any differences, we have to assume they may be
      // disambiguating, which is allowed.
      return;
    }
  }
  // If multiple name repeaters in a given context all repeat the same thing,
  // then it's obvious they don't disambiguate anything, so add Findings for
  // each violator.
  for (auto& repeater : repeaters) {
    AddRepeatedNameFinding(context, repeater);
  }
}

Linter::Linter()
    : kLibraryNameDepthCheck(DefineCheck("too-many-nested-libraries",
                                         "Avoid library names with more than two dots (or three "
                                         "dots for fuchsia.hardware libraries)")),
      kLibraryNameComponentCheck(
          DefineCheck("disallowed-library-name-component",
                      "Library names must not contain the following components: common, service, "
                      "util, base, f<letter>l, zx<word>")),
      kRepeatsLibraryNameCheck(
          DefineCheck("name-repeats-library-name",
                      "${TYPE} names (${REPEATED_NAMES}) must not repeat names from the "
                      "library '${CONTEXT_ID}'")),
      kLibraryPrefixCheck(DefineCheck("wrong-prefix-for-platform-source-library",
                                      "FIDL library name is not currently allowed")),
      kInvalidCopyrightCheck(
          DefineCheck("invalid-copyright-for-platform-source-library",
                      "FIDL files defined in the Platform Source Tree (i.e., defined in "
                      "fuchsia.googlesource.com) must begin with the standard copyright notice")),
      kCopyrightLines({
          "// Copyright ${YYYY} The Fuchsia Authors. All rights reserved.",
          "// Use of this source code is governed by a BSD-style license that can be",
          "// found in the LICENSE file.",
      }),
      kCopyrightBlock(MakeCopyrightBlock()),
      kDocAttribute("Doc"),
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

  callbacks_.OnAttribute(
      [&linter = *this,
       todo_check = DefineCheck("todo-should-not-be-doc-comment",
                                "TODO comment should use a non-flow-through comment marker"),
       regex = std::regex(R"REGEX(^[ \t]*TODO\W)REGEX")]
      //
      (const raw::Attribute& element) {
        if (element.name == linter.kDocAttribute) {
          if (std::regex_search(element.value, regex)) {
            linter.AddFinding(element, todo_check, {}, "change '///' to '//'", "//");
          }
        }
      });

  callbacks_.OnAttribute(
      [&linter = *this,
       check = DefineCheck("copyright-should-not-be-doc-comment",
                           "Copyright notice should use non-flow-through comment markers"),
       regex = std::regex(R"REGEX(^[ \t]*Copyright \d\d\d\d\W)REGEX", std::regex_constants::icase)]
      //
      (const raw::Attribute& element) {
        if (element.name == linter.kDocAttribute && std::regex_search(element.value, regex)) {
          linter.AddFinding(element, check, {}, "change '///' to '//'", "//");
        }
      });

  // TODO(fxbug.dev/FIDL-656): Remove this check after issues are resolved with
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

  callbacks_.OnUsing(
      [&linter = *this,
       case_check = DefineCheck("invalid-case-for-primitive-alias",
                                "Primitive aliases must be named in lower_snake_case"),
       &case_type = lower_snake_]
      //
      (const raw::Using& element) {
        if (element.maybe_alias != nullptr) {
          linter.CheckCase("primitive alias", element.maybe_alias, case_check, case_type);
          linter.CheckRepeatedName("primitive alias", element.maybe_alias);
        }
      });

  auto invalid_case_for_constant =
      DefineCheck("invalid-case-for-constant", "${TYPE} must be named in ALL_CAPS_SNAKE_CASE");

  callbacks_.OnConstDeclaration(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::ConstDeclaration& element) {
        linter.CheckCase("constants", element.identifier, case_check, case_type);
        linter.CheckRepeatedName("constant", element.identifier);
        linter.in_const_declaration_ = true;
      });

  callbacks_.OnExitConstDeclaration(
      [&linter = *this]
      //
      (const raw::ConstDeclaration& element) { linter.in_const_declaration_ = false; });

  callbacks_.OnEnumMember(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::EnumMember& element) {
        linter.CheckCase("enum members", element.identifier, case_check, case_type);
        linter.CheckRepeatedName("enum member", element.identifier);
      });

  callbacks_.OnBitsMember(
      [&linter = *this, case_check = invalid_case_for_constant, &case_type = upper_snake_]
      //
      (const raw::BitsMember& element) {
        linter.CheckCase("bitfield members", element.identifier, case_check, case_type);
        linter.CheckRepeatedName("bitfield member", element.identifier);
      });

  auto name_repeats_enclosing_type_name =
      DefineCheck("name-repeats-enclosing-type-name",
                  "${TYPE} names (${REPEATED_NAMES}) must not repeat names from the "
                  "enclosing ${CONTEXT_TYPE} '${CONTEXT_ID}'");

  callbacks_.OnProtocolDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name,
       name_contains_service_check = DefineCheck("protocol-name-includes-service",
                                                 "Protocols must not include the name 'service.'")]
      //
      (const raw::ProtocolDeclaration& element) {
        linter.CheckCase("protocols", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("protocol", element.identifier);
        for (auto word : utils::id_to_words(to_string(element.identifier))) {
          if (word == "service") {
            linter.AddFinding(element.identifier, name_contains_service_check);
            break;
          }
        }
        linter.EnterContext("protocol", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitProtocolDeclaration(
      [&linter = *this]
      //
      (const raw::ProtocolDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnMethod([&linter = *this]
                      //
                      (const raw::ProtocolMethod& element) {
                        linter.CheckCase("methods", element.identifier,
                                         linter.invalid_case_for_decl_name(),
                                         linter.decl_case_type_for_style());
                        linter.CheckRepeatedName("method", element.identifier);
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
        linter.CheckRepeatedName("event", element.identifier);
      });

  callbacks_.OnEnumDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name]
      //
      (const raw::EnumDeclaration& element) {
        linter.CheckCase("enums", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("enum", element.identifier);
        linter.EnterContext("enum", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitEnumDeclaration([&linter = *this]
                                   //
                                   (const raw::EnumDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnBitsDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name]
      //
      (const raw::BitsDeclaration& element) {
        linter.CheckCase("bitfields", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("bitfield", element.identifier);
        linter.EnterContext("bitfield", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitBitsDeclaration([&linter = *this]
                                   //
                                   (const raw::BitsDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnStructDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name]
      //
      (const raw::StructDeclaration& element) {
        linter.CheckCase("structs", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("struct", element.identifier);
        linter.EnterContext("struct", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitStructDeclaration(
      [&linter = *this]
      //
      (const raw::StructDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnTableDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name]
      //
      (const raw::TableDeclaration& element) {
        linter.CheckCase("tables", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("table", element.identifier);
        linter.EnterContext("table", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitTableDeclaration(
      [&linter = *this]
      //
      (const raw::TableDeclaration& element) { linter.ExitContext(); });

  callbacks_.OnUnionDeclaration(
      [&linter = *this, context_check = name_repeats_enclosing_type_name]
      //
      (const raw::UnionDeclaration& element) {
        linter.CheckCase("unions", element.identifier, linter.invalid_case_for_decl_name(),
                         linter.decl_case_type_for_style());
        linter.CheckRepeatedName("union", element.identifier);
        linter.EnterContext("union", to_string(element.identifier), context_check);
      });

  callbacks_.OnExitUnionDeclaration(
      [&linter = *this]
      //
      (const raw::UnionDeclaration& element) { linter.ExitContext(); });

  auto invalid_case_for_decl_member =
      DefineCheck("invalid-case-for-decl-member", "${TYPE} must be named in lower_snake_case");

  callbacks_.OnParameter(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::Parameter& element) {
        linter.CheckCase("parameters", element.identifier, case_check, case_type);
      });
  callbacks_.OnStructMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::StructMember& element) {
        linter.CheckCase("struct members", element.identifier, case_check, case_type);
        linter.CheckRepeatedName("struct member", element.identifier);
      });
  callbacks_.OnTableMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::TableMember& element) {
        if (element.maybe_used == nullptr)
          return;
        linter.CheckCase("table members", element.maybe_used->identifier, case_check, case_type);
        linter.CheckRepeatedName("table member", element.maybe_used->identifier);
      });
  callbacks_.OnUnionMember(
      [&linter = *this, case_check = invalid_case_for_decl_member, &case_type = lower_snake_]
      //
      (const raw::UnionMember& element) {
        if (element.maybe_used == nullptr)
          return;
        linter.CheckCase("union members", element.maybe_used->identifier, case_check, case_type);
        linter.CheckRepeatedName("union member", element.maybe_used->identifier);
      });
  // clang-format off
  callbacks_.OnTypeConstructor(
      [& linter = *this,
       string_bounds_check = DefineCheck("string-bounds-not-specified",
                                         "Specify bounds for string"),
       vector_bounds_check = DefineCheck("vector-bounds-not-specified",
                                         "Specify bounds for vector")]
      //
      (const raw::TypeConstructor& element) {
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
}

}  // namespace linter
}  // namespace fidl
