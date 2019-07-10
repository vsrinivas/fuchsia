// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_

#include <fidl/check_def.h>
#include <fidl/findings.h>
#include <fidl/linting_tree_callbacks.h>
#include <fidl/tree_visitor.h>
#include <fidl/utils.h>

#include <array>
#include <deque>
#include <regex>
#include <set>

namespace fidl {
namespace linter {

// The primary business logic for lint-checks on a FIDL file is implemented in
// the |Linter| class. This class is not thread safe.
class Linter {
 public:
  // On initialization, the |Linter| constructs the |CheckDef|
  // objects with associated check logic (lambdas registered via
  // |CheckCallbacks|).
  Linter();

  // If a user specifies the command line flag --include-check=<check-id>
  // (one or more times) without any --exclude_check flags, the default
  // state is to exclude all checks EXCEPT those specifically indicated.
  void set_exclude_by_default(bool exclude_by_default) { exclude_by_default_ = exclude_by_default; }

  void ExcludeCheckId(std::string check_id) { excluded_check_ids_.insert(check_id); }

  void IncludeCheckId(std::string check_id) { included_check_ids_.insert(check_id); }

  // Calling Lint() invokes the callbacks for elements
  // of the given |SourceFile|. If a check fails, the callback generates a
  // |Finding| and adds it to the given Findings (vector of Finding).
  // Lint() is single-threaded, and modifies state of the |Linter| class
  // as it evaluates a single file at a time.
  // Returns true if no new findings were generated.
  bool Lint(std::unique_ptr<raw::File> const& parsed_source, Findings* findings);

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
  std::string kPermittedLibraryPrefixesas_string() const;

  CheckDef DefineCheck(std::string check_id, std::string message_template);

  Finding* AddFinding(SourceLocation source_location, std::string check_id, std::string message);

  const Finding* AddFinding(SourceLocation location, const CheckDef& check,
                            Substitutions substitutions = {}, std::string suggestion_template = "",
                            std::string replacement_template = "");

  template <typename SourceElementSubtypeRefOrPtr>
  const Finding* AddFinding(const SourceElementSubtypeRefOrPtr& element, const CheckDef& check,
                            Substitutions substitutions = {}, std::string suggestion_template = "",
                            std::string replacement_template = "");

  const Finding* AddRepeatedNameFinding(const Context& context,
                                        const Context::RepeatsContextNames& name_repeater);

  bool CurrentLibraryIsPlatformSourceLibrary();
  bool CurrentFileIsInPlatformSourceTree();

  // Initialization and checks at the start of a new file. The |Linter|
  // can be called multiple times with many different files.
  void NewFile(const raw::File& element);

  // If a finding was added, return a pointer to that finding.
  const Finding* CheckCase(std::string type, const std::unique_ptr<raw::Identifier>& identifier,
                           const CheckDef& check_def, const CaseType& case_type);

  // CheckRepeatedName() does not add Finding objects immediately. It checks for
  // potential violations, but must wait until ExitContext() so the potential
  // violation can be compared to its peers.
  void CheckRepeatedName(std::string type, const std::unique_ptr<raw::Identifier>& id);

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

  std::string GetCopyrightSuggestion();
  void AddInvalidCopyrightFinding(SourceLocation location);
  void CheckInvalidCopyright(SourceLocation location, std::string line_comment,
                             std::string line_to_match);
  bool CopyrightCheckIsComplete();

  std::string MakeCopyrightBlock();

  // All check types created in during |Linter| construction. The |std::set|
  // ensures each CheckDef has a unique |id|, and an iterator will traverse
  // the set in lexicographical order.
  // Must be defined before constant checks.
  std::set<CheckDef> checks_;

  // const variables not trivially destructible (static storage is forbidden)
  // (https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables)
  const CheckDef kLibraryNameDepthCheck;
  const CheckDef kLibraryNameComponentCheck;
  const CheckDef kRepeatsLibraryNameCheck;
  const CheckDef kLibraryPrefixCheck;
  const CheckDef kInvalidCopyrightCheck;

  const std::vector<std::string> kCopyrightLines;
  const std::string kCopyrightBlock;
  const std::string kDocAttribute;
  const std::regex kYearRegex;
  const std::regex kDocCommentRegex;
  const std::regex kDisallowedLibraryComponentRegex;

  const std::set<std::string> kPermittedLibraryPrefixes;
  const std::set<std::string> kStopWords;

  std::deque<Context> context_stack_;

  size_t line_comments_checked_ = 0;

  // Set to true for the first line that does not match the standard
  // Copyright block (if checked) so subsequent lines do not have to
  // be checked. (Prevents duplicate findings.)
  bool added_invalid_copyright_finding_ = false;

  // If good copyright lines
  size_t good_copyright_lines_found_ = 0;

  // 4 digits assumed to be the intended copyright date.
  std::string copyright_date_;

  // The first name in the FIDL library declaration; for example, for:
  //   library fidl.types;
  // |library_prefix_| will be "fidl"
  std::string library_prefix_;
  bool library_is_platform_source_library_;

  std::string filename_;
  bool file_is_in_platform_source_tree_;

  LintingTreeCallbacks callbacks_;

  // Case type functions used by CheckCase().
  CaseType lower_snake_{utils::is_lower_snake_case, utils::to_lower_snake_case};
  CaseType upper_snake_{utils::is_upper_snake_case, utils::to_upper_snake_case};
  CaseType upper_camel_{utils::is_upper_camel_case, utils::to_upper_camel_case};

  bool exclude_by_default_ = false;
  std::set<std::string> included_check_ids_;
  std::set<std::string> excluded_check_ids_;

  using FindingPtr = std::unique_ptr<Finding>;

  // Compare type provides the function for ordering elements in the set, and
  // ensuring elements are set-wise unique. The current assumption is that no
  // two findings from the same subcategory at the same location are expected.
  // By including both source location and subcategory, all expected Findings
  // will be unique, and any Finding added with the same location and
  // subcategory as another will be considered a bug.
  struct FindingPtrCompare {
    // Return true if lhs < rhs.
    bool operator()(const FindingPtr& lhs, const FindingPtr& rhs) const {
      return lhs->location() < rhs->location() ||
             (lhs->location() == rhs->location() && lhs->subcategory() < rhs->subcategory());
    }
  };

  // An ordered collection of |std::unique_ptr| to |Finding| objects,
  // populated only for the duration of the Visit(), to lint a given FIDL
  // file.
  //
  // Some lint checks can only be assessed after processing more of the FIDL
  // (for example, "name-repeats-enclosing-type-name" checks cannot be added
  // until all members of the enclosing type are available). Other checks on
  // the same members will be added as they are encountered. Since |Finding|
  // objects are collected out of source order, an ordered collecttion
  // ensures |Finding| objects are in sorted order.
  //
  // |Finding| objects must be returned in source order (filename, starting
  // character, and ending character).
  //
  // |Finding| objects are sorted by SourceLocation, with additional criteria
  // to sort multiple findings for the same source element. If a Finding is
  // added to an ordered collection that does not allow duplicates, and the
  // sort criteria is not specific enough to avoid a key collision, an
  // assertion error will halt the program, to be resolved either by adding
  // additional sort criteriea or changing the Finding objects to ensure
  // uinqueness.
  //
  // When the Visit() is over, the |Finding| objects are transferred, in order
  // (by filename and source location of each finding, with any duplicates
  // also included in the order they were discovered/added) into the |Findings|
  // list passed into the Lint() method, and |current_findings_| is cleared.
  std::set<FindingPtr, FindingPtrCompare> current_findings_;
};

}  // namespace linter
}  // namespace fidl
#endif  // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LINTER_H_
