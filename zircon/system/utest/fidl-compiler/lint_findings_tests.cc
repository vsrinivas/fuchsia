// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <utility>

#include <fidl/findings.h>
#include <fidl/template_string.h>
#include <fidl/utils.h>

#include "test_library.h"
#include "unittest_helpers.h"

namespace fidl {

namespace {

#define ASSERT_FINDINGS(TEST) ASSERT_NO_FATAL_FAILURES(TEST.ExpectFindings())

#define ASSERT_FINDINGS_IN_ANY_POSITION(TEST) \
  ASSERT_NO_FATAL_FAILURES(TEST.ExpectFindingsInAnyPosition())

#define ASSERT_NO_FINDINGS(TEST) ASSERT_NO_FATAL_FAILURES(TEST.ExpectNoFindings())

class LintTest {
 public:
  LintTest() = default;

  // Adds a Finding to the back of the list of Findings.
  LintTest& AddFinding(const std::string& check_id, const std::string& message,
                       const std::string& violation_string = "${TEST}", std::string suggestion = "",
                       std::string replacement = "") {
    assert(!source_template_.str().empty() &&
           "source_template() must be called before AddFinding()");
    std::string template_string = source_template_.str();
    size_t start = template_string.find(violation_string);
    if (start == std::string::npos) {
      std::cout << "ERROR: violation_string '" << violation_string
                << "' was not found in template string:" << std::endl
                << template_string;
    }
    // Note, if there are any substitution variables in the template that
    // precede the violation_string, the test will probably fail because the
    // string location will probably be different after substitution.
    assert(start != std::string::npos && "Bad test! violation_string not found in template");
    std::string expanded_violation_string =
        TemplateString(violation_string).Substitute(substitutions_);
    auto span = library().source_span(start, expanded_violation_string.size());
    auto& finding = expected_findings_.emplace_back(span, check_id, message);

    if (suggestion.empty()) {
      suggestion = default_suggestion_;
    }
    if (replacement.empty()) {
      replacement = default_replacement_;
    }
    if (!suggestion.empty()) {
      if (replacement.empty()) {
        finding.SetSuggestion(suggestion);
      } else {
        finding.SetSuggestion(suggestion, replacement);
      }
    }

    return *this;
  }

  // Adds a Finding to the back of the list of Findings using the default
  // check_id and message (via previous calls to check_id() and message()).
  LintTest& AddFinding(const std::string& violation_string = "${TEST}") {
    return AddFinding(default_check_id_, default_message_, violation_string);
  }

  // Optional description of what is being tested. This can help when
  // reading the code or debugging a failed test, particularly if
  // it's not obvious what is being tested.
  // |that_| is automatically cleared after test execution in case
  // a follow-up test with a different purpose does not set a new
  // value.
  LintTest& that(std::string that) {
    that_ = std::move(that);
    return *this;
  }

  LintTest& filename(std::string filename) {
    filename_ = std::move(filename);
    return *this;
  }

  LintTest& check_id(std::string check_id) {
    default_check_id_ = std::move(check_id);
    return *this;
  }

  LintTest& message(std::string message) {
    default_message_ = std::move(message);
    return *this;
  }

  LintTest& suggestion(const std::string& suggestion) {
    default_suggestion_ = suggestion;
    if (!expected_findings_.empty()) {
      Finding& finding = expected_findings_.back();
      finding.SetSuggestion(suggestion);
    }
    return *this;
  }

  LintTest& replacement(const std::string& replacement) {
    default_replacement_ = replacement;
    if (!expected_findings_.empty()) {
      Finding& finding = expected_findings_.back();
      assert(finding.suggestion().has_value() && "|suggestion| must be added before |replacement|");
      auto description = finding.suggestion()->description();
      finding.SetSuggestion(description, replacement);
    }
    return *this;
  }

  LintTest& source_template(std::string template_str) {
    source_template_ = TemplateString(std::move(template_str));
    return *this;
  }

  LintTest& substitute(const Substitutions& substitutions) {
    substitutions_ = substitutions;
    return *this;
  }

  // Shorthand for the common occurrence of a single substitution variable.
  LintTest& substitute(const std::string& var_name, const std::string& value) {
    return substitute({{var_name, value}});
  }

  LintTest& include_checks(std::vector<std::string> included_check_ids) {
    included_check_ids_ =
        std::set<std::string>(included_check_ids.begin(), included_check_ids.end());
    return *this;
  }

  LintTest& exclude_checks(std::vector<std::string> excluded_check_ids) {
    excluded_check_ids_ =
        std::set<std::string>(excluded_check_ids.begin(), excluded_check_ids.end());
    return *this;
  }

  LintTest& excluded_checks_to_confirm(std::vector<std::string> excluded_check_ids_to_confirm) {
    excluded_check_ids_to_confirm_ = std::set<std::string>(excluded_check_ids_to_confirm.begin(),
                                                           excluded_check_ids_to_confirm.end());
    return *this;
  }

  LintTest& exclude_by_default(bool exclude_by_default) {
    exclude_by_default_ = exclude_by_default;
    return *this;
  }

  void ExpectNoFindings() { execute(/*expect_findings=*/false); }

  void ExpectFindings() { execute(/*expect_findings=*/true); }

  void ExpectFindingsInAnyPosition() {
    execute(/*expect_findings=*/true,
            /*assert_positions_match=*/false);
  }

 private:
  // Removes all expected findings previously added with AddFinding().
  void execute_helper(bool expect_findings, bool assert_positions_match) {
    std::ostringstream ss;
    if (default_check_id_.empty()) {
      ss << std::endl << "Failed test";
    } else {
      ss << std::endl << "Failed test for check '" << default_check_id_ << "'";
    }
    if (!that_.empty()) {
      ss << std::endl << "that " << that_;
    }
    ss << ":" << std::endl;

    // Start with checks for invalid test construction:
    auto context = (ss.str() + "Bad test!");

    if (expect_findings && expected_findings_.empty()) {
      ASSERT_FALSE(default_message_.empty(), "%s", context.c_str());
      AddFinding(default_check_id_, default_message_);
    }

    ASSERT_FALSE((!expect_findings) && (!expected_findings_.empty()), "%s", context.c_str());

    ASSERT_NO_FATAL_FAILURES(ValidTest(), "%s", context.c_str());

    // The test looks good, so run the linter, and update the context
    // value by replacing "Bad test!" with the FIDL source code.
    Findings findings;
    bool passed = library().Lint(&findings, included_check_ids_, excluded_check_ids_,
                                 exclude_by_default_, &excluded_check_ids_to_confirm_);

    EXPECT_TRUE(passed == (findings.empty()));

    if (!excluded_check_ids_to_confirm_.empty()) {
      ss << "Excluded check-ids not found: " << std::endl;
      for (auto& check_id : excluded_check_ids_to_confirm_) {
        ss << "  * " << check_id << std::endl;
      }
      context = ss.str();
      EXPECT_TRUE(excluded_check_ids_to_confirm_.empty(), "%s", context.c_str());
    }

    std::string source_code = std::string(library().source_file().data());
    if (source_code.back() == '\0') {
      source_code.resize(source_code.size() - 1);
    }
    ss << source_code;
    context = ss.str();

    auto finding = findings.begin();
    auto expected_finding = expected_findings_.begin();

    while (finding != findings.end() && expected_finding != expected_findings_.end()) {
      CompareExpectedToActualFinding(*expected_finding, *finding, ss.str(), assert_positions_match);
      expected_finding++;
      finding++;
    }
    if (finding != findings.end()) {
      PrintFindings(ss, finding, findings.end(), "UNEXPECTED FINDINGS");
      context = ss.str();
      bool has_unexpected_findings = true;
      EXPECT_FALSE(has_unexpected_findings, "%s", context.c_str());
    }
    if (expected_finding != expected_findings_.end()) {
      PrintFindings(ss, expected_finding, expected_findings_.end(), "EXPECTED FINDINGS NOT FOUND");
      context = ss.str();
      bool expected_findings_not_found = true;
      EXPECT_FALSE(expected_findings_not_found, "%s", context.c_str());
    }
  }

  void Reset() {
    library_.reset();
    expected_findings_.clear();
    included_check_ids_.clear();
    excluded_check_ids_.clear();
    excluded_check_ids_to_confirm_.clear();
    exclude_by_default_ = false;
    that_ = "";
  }

  void execute(bool expect_findings, bool assert_positions_match = true) {
    execute_helper(expect_findings, assert_positions_match);
    Reset();
  }

  void ValidTest() const {
    ASSERT_FALSE(source_template_.str().empty(), "Missing source template");
    if (!substitutions_.empty()) {
      ASSERT_FALSE(source_template_.Substitute(substitutions_, false) !=
                       source_template_.Substitute(substitutions_, true),
                   "Missing template substitutions");
    }
    if (expected_findings_.empty()) {
      ASSERT_FALSE(default_check_id_.empty(), "Missing check_id");
    } else {
      auto& expected_finding = expected_findings_.front();
      ASSERT_FALSE(expected_finding.subcategory().empty(), "Missing check_id");
      ASSERT_FALSE(expected_finding.message().empty(), "Missing message");
      ASSERT_FALSE(!expected_finding.span().valid(), "Missing position");
    }
  }

  // Complex templates with more than one substitution variable will typically
  // throw off the location match. Set |assert_positions_match| to false to
  // skip this check.
  static void CompareExpectedToActualFinding(const Finding& expectf, const Finding& finding,
                                             const std::string& test_context,
                                             bool assert_positions_match) {
    std::ostringstream ss;
    ss << finding.span().position_str() << ": ";
    utils::PrintFinding(ss, finding);
    auto context = (test_context + ss.str());
    ASSERT_STRING_EQ(expectf.subcategory(), finding.subcategory(), "%s", context.c_str());
    if (assert_positions_match) {
      ASSERT_STRING_EQ(expectf.span().position_str(), finding.span().position_str(), "%s",
                       context.c_str());
    }
    ASSERT_STRING_EQ(expectf.message(), finding.message(), "%s", context.c_str());
    ASSERT_EQ(expectf.suggestion().has_value(), finding.suggestion().has_value(), "%s",
              context.c_str());
    if (finding.suggestion().has_value()) {
      ASSERT_STRING_EQ(expectf.suggestion()->description(), finding.suggestion()->description(),
                       "%s", context.c_str());
      ASSERT_EQ(expectf.suggestion()->replacement().has_value(),
                finding.suggestion()->replacement().has_value(), "%s", context.c_str());
      if (finding.suggestion()->replacement().has_value()) {
        ASSERT_STRING_EQ(*expectf.suggestion()->replacement(), *finding.suggestion()->replacement(),
                         "%s", context.c_str());
      }
    }
  }

  template <typename Iter>
  void PrintFindings(std::ostream& os, Iter finding, Iter end, const std::string& title) {
    os << "\n\n";
    os << "============================" << std::endl;
    os << title << ":" << std::endl;
    os << "============================" << std::endl;
    for (; finding != end; finding++) {
      os << finding->span().position_str() << ": ";
      utils::PrintFinding(os, *finding);
      os << std::endl;
    }
    os << "============================" << std::endl;
  }

  TestLibrary& library() {
    if (!library_) {
      assert(!source_template_.str().empty() &&
             "source_template() must be set before library() is called");
      library_ =
          std::make_unique<TestLibrary>(filename_, source_template_.Substitute(substitutions_));
    }
    return *library_;
  }

  std::string that_;  // optional description of what is being tested
  std::string filename_ = "example.fidl";
  std::string default_check_id_;
  std::string default_message_;
  std::string default_suggestion_;
  std::string default_replacement_;
  std::set<std::string> included_check_ids_;
  std::set<std::string> excluded_check_ids_;
  std::set<std::string> excluded_check_ids_to_confirm_;
  bool exclude_by_default_ = false;
  Findings expected_findings_;
  TemplateString source_template_;
  Substitutions substitutions_;

  std::unique_ptr<TestLibrary> library_;
};

TEST(LintFindingsTests, ModifierOrder) {
  LintTest test;
  test.check_id("modifier-order").source_template(R"FIDL(
library fidl.a;

type MyUnion = ${TEST} union {
  1: foo bool;
};
)FIDL");

  test.substitute("TEST", "flexible");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "strict");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "flexible resource");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "strict resource");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "resource flexible")
      .message("Strictness modifier on union must always precede the resource modifier")
      .suggestion("move 'flexible' modifier before resource modifier for union");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "resource strict")
      .message("Strictness modifier on union must always precede the resource modifier")
      .suggestion("move 'strict' modifier before resource modifier for union");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ExplicitFlexibleModifier) {
  std::map<std::string, std::string> named_templates = {
      {"bitfield", R"FIDL(
library fidl.a;

type MyBits = ${TEST}bits : uint32 {
  VALUE = 1;
};
)FIDL"},
      {"enum", R"FIDL(
library fidl.a;

type MyEnum = ${TEST}enum {
  VALUE = 1;
};
)FIDL"},
      {"union", R"FIDL(
library fidl.a;

type MyUnion = ${TEST}resource union {
  1: member bool;
};
)FIDL"},
      {"union", R"FIDL(
library fidl.a;

type MyStruct = struct{
  inner ${TEST}resource union {
    1: member bool;
  };
};
)FIDL"},
      {"bitfield", R"FIDL(
library fidl.a;

protocol Foo {
  Bar(struct {
    options ${TEST}bits {
      OPTION_A = 1;
      OPTION_B = 2;
    }
  });
};
)FIDL"},
  };

  for (auto const& named_template : named_templates) {
    LintTest test;
    test.check_id("explicit-flexible-modifier").source_template(named_template.second);

    test.substitute("TEST", "flexible ");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "strict ");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "")
        .message(named_template.first + " must have an explicit 'flexible' modifier")
        .suggestion("add 'flexible' modifier before " + named_template.first + " keyword");
    ASSERT_FINDINGS(test);
  }
}

TEST(LintFindingsTests, ConstantShouldUseCommonPrefixSuffixPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning for "MINIMUM_..." or "MAXIMUM...", or maybe(?) "..._CAP" Also for instance
  // "SET_CLIENT_NAME_MAX_LEN" -> "MAX_CLIENT_NAME_LEN" or MAX_LEN_CLIENT_NAME", so detect
  // "_MAX" or "_MIN" as separate words in middle or at end of identifier.

  LintTest test;
  test.check_id("constant-should-use-common-prefix-suffix")
      .message(
          "Constants should use the standard prefix and/or suffix for common concept, "
          "such as MIN and MAX, rather than MINIMUM and MAXIMUM, respectively.")
      .source_template(R"FIDL(
library fidl.a;

const ${TEST} uint64 = 1234;
)FIDL");

  test.substitute("TEST", "MIN_HEIGHT");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "MAX_HEIGHT");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "NAME_MIN_LEN");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "NAME_MAX_LEN");
  ASSERT_NO_FINDINGS(test);

  // Not yet determined if the standard should be LEN or LENGTH, or both
  // test.substitute("TEST", "BYTES_LEN");
  // ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "THRESHOLD_MIN");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "THRESHOLD_MAX");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "MINIMUM_HEIGHT")
      .suggestion("change 'MINIMUM_HEIGHT' to 'MIN_HEIGHT'")
      .replacement("MIN_HEIGHT");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "MAXIMUM_HEIGHT")
      .suggestion("change 'MAXIMUM_HEIGHT' to 'MAX_HEIGHT'")
      .replacement("MAX_HEIGHT");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "NAME_MINIMUM_LEN")
      .suggestion("change 'NAME_MINIMUM_LEN' to 'NAME_MIN_LEN'")
      .replacement("NAME_MIN_LEN");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "NAME_MAXIMUM_LEN")
      .suggestion("change 'NAME_MAXIMUM_LEN' to 'NAME_MAX_LEN'")
      .replacement("NAME_MAX_LEN");
  ASSERT_FINDINGS(test);

  // Not yet determined if the standard should be LEN or LENGTH, or both
  // test.substitute("TEST", "BYTES_LENGTH")
  //     .suggestion("change 'BYTES_LENGTH' to 'BYTES_LEN'")
  //     .replacement("BYTES_LEN");
  // ASSERT_FINDINGS(test);

  test.substitute("TEST", "THRESHOLD_MINIMUM")
      .suggestion("change 'THRESHOLD_MINIMUM' to 'THRESHOLD_MIN'")
      .replacement("THRESHOLD_MIN");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "THRESHOLD_MAXIMUM")
      .suggestion("change 'THRESHOLD_MAXIMUM' to 'THRESHOLD_MAX'")
      .replacement("THRESHOLD_MAX");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "THRESHOLD_CAP")
      .suggestion("change 'THRESHOLD_CAP' to 'THRESHOLD_MAX'")
      .replacement("THRESHOLD_MAX");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, CopyrightShouldNotBeDocComment) {
  LintTest test;
  test.check_id("copyright-should-not-be-doc-comment")
      .message("Copyright notice should use non-flow-through comment markers")
      .source_template(R"FIDL(${TEST} Copyright 2019 The Fuchsia Authors. All rights reserved.
${TEST} Use of this source code is governed by a BSD-style license that can be
${TEST} found in the LICENSE file.
library fidl.a;
)FIDL");

  test.substitute("TEST", "//");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "///").suggestion("change '///' to '//'").replacement("//");
  ASSERT_FINDINGS(test);

  test.that("capitalization is not important")
      .source_template(R"FIDL(${TEST} copyright 2019 The Fuchsia Authors. All rights reserved.
${TEST} Use of this source code is governed by a BSD-style license that can be
${TEST} found in the LICENSE file.
library fidl.a;
)FIDL");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, DisallowedLibraryNameComponent) {
  LintTest test;
  test.check_id("disallowed-library-name-component")
      .message(
          "Library names must not contain the following components: common, service, util, "
          "base, f<letter>l, zx<word>")
      .source_template(R"FIDL(
library fidl.${TEST};
)FIDL");

  test.substitute("TEST", "display");
  ASSERT_NO_FINDINGS(test);

  // Bad test: zx<word>
  test.substitute("TEST", "zxsocket");
  // no suggestion
  ASSERT_FINDINGS(test);

  // Bad test: f<letter>l
  test.substitute("TEST", "ful");
  // no suggestion
  ASSERT_FINDINGS(test);

  // Bad test: banned words like "common"
  test.substitute("TEST", "common");
  // no suggestion
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ProtocolNameIncludesService) {
  // Error if ends in "Service", warning if includes "Service" as a word, but "Serviceability"
  // ("Service" is only part of a word) is OK.

  LintTest test;
  test.check_id("protocol-name-includes-service")
      .message("Protocols must not include the name 'service.'")
      .source_template(R"FIDL(
library fidl.a;

protocol ${TEST} {};
)FIDL");

  test.substitute("TEST", "TestProtocol");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "ServiceabilityProtocol");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "TestServiceabilityProtocol");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "ProtocolForServiceability");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "Service");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "ServiceProtocol");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "TestServiceProtocol");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "ProtocolForService");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, EventNamesMustStartWithOn) {
  LintTest test;
  test.check_id("event-names-must-start-with-on")
      .message("Event names must start with 'On'")
      .source_template(R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> ${TEST}();
};
)FIDL");

  test.substitute("TEST", "OnPress");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "Press").suggestion("change 'Press' to 'OnPress'").replacement("OnPress");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "OntologyUpdate")
      .suggestion("change 'OntologyUpdate' to 'OnOntologyUpdate'")
      .replacement("OnOntologyUpdate");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ExcessiveNumberOfSeparateProtocolsForFilePleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning(?) if a fidl file contains more than some tolerance cap number of protocols.
  //
  // Or if a directory of fidl files contains more than some tolerance number of files AND any
  // fidl file(s) in that directory contains more than some smaller cap number of protocols per
  // fidl file. The fuchsia.ledger would be a good one to look at since it defines many protocols.
  // We do not have public vs private visibility yet, and the cap may only be needed for public
  // things.

  LintTest test;
  test.check_id("excessive-number-of-separate-protocols-for-file")
      .message(
          "Some libraries create separate protocol instances for every logical object in "
          "the protocol, but this approach has a number of disadvantages:")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ExcessiveNumberOfSeparateProtocolsForLibraryPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Or if a directory of fidl files contains more than some tolerance number of files AND any
  // fidl file(s) in that directory contains more than some smaller cap number of protocols per
  // fidl file. The fuchsia.ledger would be a good one to look at since it defines many protocols.
  // We do not have public vs private visibility yet, and the cap may only be needed for public
  // things.

  LintTest test;
  test.check_id("excessive-number-of-separate-protocols-for-library")
      .message(
          "Some libraries create separate protocol instances for every logical object in "
          "the protocol, but this approach has a number of disadvantages:")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, InconsistentTypeForRecurringFileConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  LintTest test;
  test.check_id("inconsistent-type-for-recurring-file-concept")
      .message("Use consistent types for the same concept")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, InconsistentTypeForRecurringLibraryConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  LintTest test;
  test.check_id("inconsistent-type-for-recurring-library-concept")
      .message("Ideally, types would be used consistently across library boundaries")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, InvalidCaseForConstant) {
  std::map<std::string, std::string> named_templates = {
      {"constants", R"FIDL(
library fidl.a;

const ${TEST} uint64 = 1234;
)FIDL"},
      {"enum members", R"FIDL(
library fidl.a;

type Int8Enum = flexible enum : int8 {
    ${TEST} = -1;
};
)FIDL"},
      {"bitfield members", R"FIDL(
library fidl.a;

type Uint32Bitfield = flexible bits : uint32 {
  ${TEST} = 0x00000004;
};
)FIDL"},
  };

  for (auto const& named_template : named_templates) {
    LintTest test;
    test.check_id("invalid-case-for-constant")
        .message(named_template.first + " must be named in ALL_CAPS_SNAKE_CASE")
        .source_template(named_template.second);

    test.substitute("TEST", "SOME_CONST");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "some_CONST")
        .suggestion("change 'some_CONST' to 'SOME_CONST'")
        .replacement("SOME_CONST");
    ASSERT_FINDINGS(test);

    test.substitute("TEST", "kSomeConst")
        .suggestion("change 'kSomeConst' to 'SOME_CONST'")
        .replacement("SOME_CONST");
    ASSERT_FINDINGS(test);
  }
}

TEST(LintFindingsTests, InvalidCaseForDeclMember) {
  std::map<std::string, std::string> named_templates = {
      {"struct members", R"FIDL(
library fidl.a;

protocol TestProtocol {
    SomeMethod(struct { ${TEST} string:64; });
};
)FIDL"},
      {"struct members", R"FIDL(
library fidl.a;

type DeclName = struct {
    ${TEST} string:64;
};
)FIDL"},
      {"table members", R"FIDL(
library fidl.a;

type DeclName = table {
    1: ${TEST} string:64;
};
)FIDL"},
      {"union members", R"FIDL(
library fidl.a;

type DeclName = flexible union {
    1: ${TEST} string:64;
};
)FIDL"},
      {"union members", R"FIDL(
library fidl.a;

type DeclName = struct {
    decl_member flexible union {
        1: ${TEST} string:64;
    };
};
)FIDL"},
  };

  for (auto const& named_template : named_templates) {
    LintTest test;
    test.check_id("invalid-case-for-decl-member")
        .message(named_template.first + " must be named in lower_snake_case")
        .source_template(named_template.second);

    test.substitute("TEST", "agent_request_count");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "agentRequestCount")
        .suggestion("change 'agentRequestCount' to 'agent_request_count'")
        .replacement("agent_request_count");
    ASSERT_FINDINGS(test);
  }
}

TEST(LintFindingsTests, InvalidCaseForDeclNameCStyle) {
  std::map<std::string, std::string> named_templates = {
      {"protocols", R"FIDL(
library zx;

protocol ${TEST} {};
)FIDL"},
      {"methods", R"FIDL(
library zx;

protocol test_protocol {
  ${TEST}();
};
)FIDL"},
      {"enums", R"FIDL(
library zx;

type ${TEST} = flexible enum : int8 {
    SOME_CONST = -1;
};
)FIDL"},
      {"bitfields", R"FIDL(
library zx;

type ${TEST} = flexible bits : uint32 {
  SOME_BIT = 0x00000004;
};
)FIDL"},
      {"structs", R"FIDL(
library zx;

type ${TEST} = struct {
    decl_member string:64;
};
)FIDL"},
      {"tables", R"FIDL(
library zx;

type ${TEST} = table {
    1: decl_member string:64;
};
)FIDL"},
      {"unions", R"FIDL(
library zx;

type ${TEST} = flexible union {
    1: decl_member string:64;
};
)FIDL"},
  };

  for (auto const& named_template : named_templates) {
    LintTest test;
    test.check_id("invalid-case-for-decl-name")
        .message(named_template.first + " must be named in lower_snake_case")
        .source_template(named_template.second);

    test.substitute("TEST", "url_loader");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "URLLoader")
        .suggestion("change 'URLLoader' to 'url_loader'")
        .replacement("url_loader");
    ASSERT_FINDINGS(test);

    test.substitute("TEST", "UrlLoader")
        .suggestion("change 'UrlLoader' to 'url_loader'")
        .replacement("url_loader");
    ASSERT_FINDINGS(test);
  }
}

TEST(LintFindingsTests, InvalidCaseForDeclNameIpcStyle) {
  std::map<std::string, std::string> named_templates = {
      {"protocols", R"FIDL(
library fidl.a;

protocol ${TEST} {};
)FIDL"},
      {"methods", R"FIDL(
library fidl.a;

protocol TestProtocol {
  ${TEST}();
};
)FIDL"},
      {"enums", R"FIDL(
library fidl.a;

type ${TEST} = flexible enum : int8 {
    SOME_CONST = -1;
};
)FIDL"},
      {"bitfields", R"FIDL(
library fidl.a;

type ${TEST} = flexible bits : uint32 {
  SOME_BIT = 0x00000004;
};
)FIDL"},
      {"structs", R"FIDL(
library fidl.a;

type ${TEST} = struct {
    decl_member string:64;
};
)FIDL"},
      {"tables", R"FIDL(
library fidl.a;

type ${TEST} = table {
    1: decl_member string:64;
};
)FIDL"},
      {"unions", R"FIDL(
library fidl.a;

type ${TEST} = flexible union {
    1: decl_member string:64;
};
)FIDL"},
  };

  for (auto const& named_template : named_templates) {
    LintTest test;
    test.check_id("invalid-case-for-decl-name")
        .message(named_template.first + " must be named in UpperCamelCase")
        .source_template(named_template.second);

    test.substitute("TEST", "UrlLoader");
    ASSERT_NO_FINDINGS(test);

    test.substitute("TEST", "URLLoader")
        .suggestion("change 'URLLoader' to 'UrlLoader'")
        .replacement("UrlLoader");
    ASSERT_FINDINGS(test);

    test.substitute("TEST", "url_loader")
        .suggestion("change 'url_loader' to 'UrlLoader'")
        .replacement("UrlLoader");
    ASSERT_FINDINGS(test);
  }
}

TEST(LintFindingsTests, InvalidCaseForDeclNameForEvent) {
  LintTest test;
  test.check_id("invalid-case-for-decl-name")
      .message("events must be named in UpperCamelCase")
      .source_template(R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> ${TEST}();
};
)FIDL");

  test.substitute("TEST", "OnUrlLoader");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "OnURLLoader")
      .suggestion("change 'OnURLLoader' to 'OnUrlLoader'")
      .replacement("OnUrlLoader");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, InvalidCaseForPrimitiveAlias) {
  LintTest test;
  test.check_id("invalid-case-for-using-alias")
      .message("Using aliases must be named in lower_snake_case")
      .source_template(R"FIDL(
library fidl.a;

using zx as ${TEST};

alias unused = ${TEST}.handle;
)FIDL");

  test.substitute("TEST", "what_if_someone_does_this");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "WhatIfSomeoneDoes_This")
      .suggestion("change 'WhatIfSomeoneDoes_This' to 'what_if_someone_does_this'")
      .replacement("what_if_someone_does_this");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, InvalidCopyrightForPlatformSourceLibrary) {
  TemplateString copyright_template(
      R"FIDL(// Copyright ${YYYY} The Fuchsia Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.)FIDL");
  auto copyright_2019 = copyright_template.Substitute({{"YYYY", "2019"}});
  auto copyright_2020 = copyright_template.Substitute({{"YYYY", "2020"}});

  LintTest test;
  test.filename("fuchsia/example.fidl")
      .check_id("invalid-copyright-for-platform-source-library")
      .message(
          "FIDL files defined in the Platform Source Tree (i.e., defined in "
          "fuchsia.googlesource.com) must begin with the standard copyright notice");

  test.source_template(copyright_2019 + R"FIDL(

library fidl.a;
)FIDL");
  ASSERT_NO_FINDINGS(test);

  TemplateString copyright_with_all_rights_reserved(
      R"FIDL(// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.)FIDL");

  test.source_template(copyright_2019 + R"FIDL(

library fidl.a;
)FIDL");
  ASSERT_NO_FINDINGS(test);

  test.that("the rubric does not mandate a blank line before the library name")
      .source_template(copyright_2019 + R"FIDL(
library fidl.a;
)FIDL");
  ASSERT_NO_FINDINGS(test);

  test.that("the the date doesn't have to match").source_template(copyright_2020 + R"FIDL(

library fidl.a;
)FIDL");
  ASSERT_NO_FINDINGS(test);

  test.that("the copyright must start on the first line")
      .source_template("\n" + copyright_2019 + R"FIDL(

library fidl.a;
)FIDL")
      .suggestion("Insert missing header:\n\n" + copyright_2019)
      .AddFinding("Copyright");
  ASSERT_FINDINGS(test);

  test.that("a bad or missing date will produce a suggestion with ${YYYY}")
      .source_template(R"FIDL(// Copyright 20019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Insert missing header:\n\n" + copyright_template.str())
      .AddFinding("20019");
  ASSERT_FINDINGS(test);

  test.that("the words must have the correct case")
      .source_template(R"FIDL(// COPYRIGHT 2019 THE FUCHSIA AUTHORS. ALL RIGHTS RESERVED.
    // USE OF THIS SOURCE CODE IS GOVERNED BY A BSD-STYLE LICENSE THAT CAN BE
    // FOUND IN THE LICENSE FILE.

library fidl.a;
)FIDL")
      .suggestion("Insert missing header:\n\n" + copyright_2019)
      .AddFinding("OPYRIGHT");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Sloppyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Insert missing header:\n\n" + copyright_2019)
      .AddFinding("Sloppyright");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright 2019 The Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Insert missing header:\n\n" + copyright_2019)
      .AddFinding("Authors");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by an anarchy license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Update your header with:\n\n" + copyright_2019)
      .AddFinding("n anarchy");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the README.md file.

library fidl.a;
)FIDL")
      .suggestion("Update your header with:\n\n" + copyright_2019)
      .AddFinding("README.md");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright ${YYYY} The Fuchsia Authors. All rights reserved.
/// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Update your header with:\n\n" + copyright_template.str())
      .AddFinding("// Copyright");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
/// found in the LICENSE file.

library fidl.a;
)FIDL")
      .suggestion("Update your header with:\n\n" + copyright_2019)
      .AddFinding("// Copyright");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be

library fidl.a;
)FIDL")
      .suggestion("Update your header with:\n\n" + copyright_2019)
      .AddFinding("// Copyright");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(${BLANK_LINE}
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library fidl.a;
)FIDL")
      .substitute("BLANK_LINE", "")
      .suggestion("Update your header with:\n\n" + copyright_template.str())
      .AddFinding("${BLANK_LINE}");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, LibraryNameDoesNotMatchFilePathPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  LintTest test;
  test.check_id("library-name-does-not-match-file-path")
      .message(
          "The <library> directory is named using the dot-separated name of the FIDL "
          "library")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ManagerProtocolsAreDiscouragedPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  LintTest test;
  test.check_id("manager-protocols-are-discouraged")
      .message(
          "The name Manager may be used as a name of last resort for a protocol with broad "
          "scope")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, MethodReturnStatusMissingOkPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning or error(?) if returning a "status" enum that does not have an OK value. Note there
  // will be (or is) new guidance here.
  //
  // From the rubric:
  //
  //   If a method can return either an error or a result, use the following pattern:
  //
  //     enum MyStatus { OK; FOO; BAR; ... };
  //
  //     protocol Frobinator {
  //         1: Frobinate(...) -> (MyStatus status, FrobinateResult? result);
  //     };

  LintTest test;
  test.check_id("method-return-status-missing-ok")
      .message("")  // TBD
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, MethodReturnsStatusWithNonOptionalResultPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning if return a status and a non-optional result? we now have another more expressive
  // pattern for this, this section should be updated. Specifically, see:
  // https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-014.md.

  LintTest test;
  test.check_id("method-returns-status-with-non-optional-result")
      .message("")  // TBD
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, MethodShouldPipelineProtocolsPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Error(?) if the return tuple contains one value of another FIDL protocol type. Returning a
  // protocol is better done by sending a request for pipelining. This will be hard to lint at the
  // raw level, because you do not know to differentiate Bar from a protocol vs a message vs a bad
  // name since resolution is done later. This may call for linting to be done on the JSON IR.

  LintTest test;
  test.check_id("method-should-pipeline-protocols")
      .message("")  // TBD
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, NoCommonlyReservedWordsPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  LintTest test;
  test.check_id("no-commonly-reserved-words")
      .message("Avoid commonly reserved words")
      .source_template(R"FIDL(
library fidl.a;

using zx as ${TEST};
)FIDL");

  // Unique union of reserved words from:
  // FIDL, C++, Rust, Dart, Go, Java, JavaScript, and TypeScript
  auto checked_words = {
      "_",
      "abstract",
      "and",
      "and_eq",
      "any",
      "array",
      "as",
      "asm",
      "assert",
      "async",
      "auto",
      "await",
      "become",
      "bitand",
      "bitor",
      "bits",
      "bool",
      "boolean",
      "box",
      "break",
      "byte",
      "case",
      "catch",
      "chan",
      "char",
      "class",
      "compl",
      "const",
      "const_cast",
      "constructor",
      "continue",
      "covariant",
      "crate",
      "debugger",
      "declare",
      "default",
      "defer",
      "deferred",
      "delete",
      "do",
      "double",
      "dyn",
      "dynamic",
      "dynamic_cast",
      "else",
      "enum",
      "error",
      "explicit",
      "export",
      "extends",
      "extern",
      "external",
      "factory",
      "fallthrough",
      "false",
      "final",
      "finally",
      "float",
      "fn",
      "for",
      "friend",
      "from",
      "func",
      "function",
      "get",
      "go",
      "goto",
      "handle",
      "hide",
      "if",
      "impl",
      "implements",
      "import",
      "in",
      "inline",
      "instanceof",
      "int",
      "protocol",
      "is",
      "let",
      "library",
      "long",
      "loop",
      "macro",
      "map",
      "match",
      "mixin",
      "mod",
      "module",
      "move",
      "mut",
      "mutable",
      "namespace",
      "native",
      "new",
      "not",
      "not_eq",
      "null",
      "number",
      "of",
      "on",
      "operator",
      "or",
      "or_eq",
      "override",
      "package",
      "part",
      "priv",
      "private",
      "protected",
      "protocol",
      "pub",
      "public",
      "range",
      "ref",
      "register",
      "reinterpret_cast",
      "request",
      "require",
      "reserved",
      "rethrow",
      "return",
      "select",
      "self",
      "set",
      "short",
      "show",
      "signed",
      "sizeof",
      "static",
      "static_cast",
      "strictfp",
      "string",
      "struct",
      "super",
      "switch",
      "symbol",
      "sync",
      "synchronized",
      "table",
      "template",
      "this",
      "throw",
      "throws",
      "trait",
      "transient",
      "true",
      "try",
      "type",
      "typedef",
      "typeid",
      "typename",
      "typeof",
      "union",
      "unsafe",
      "unsigned",
      "unsized",
      "use",
      "using",
      "var",
      "vector",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "where",
      "while",
      "with",
      "xor",
      "xor_eq",
      "xunion",
      "yield",
  };

  for (auto word : checked_words) {
    test.substitute("TEST", word);
    ASSERT_FINDINGS(test);
  }
}

// TODO(fxbug.dev/7978): Remove this check after issues are resolved with
// trailing comments in existing source and tools
TEST(LintFindingsTests, NoTrailingComment) {
  LintTest test;
  test.check_id("no-trailing-comment")
      .message("Place comments above the thing being described")
      .source_template(R"FIDL(
library fidl.a;

type SeasonToShirtAndPantMapEntry = struct {

  // winter, spring, summer, or fall
  season string:64;

  // all you gotta do is call
  shirt_and_pant_type string:64;

  clashes bool;
};
)FIDL");

  ASSERT_NO_FINDINGS(test);

  test.source_template(R"FIDL(
library fidl.a;

type SeasonToShirtAndPantMapEntry = struct {

  season string:64; // winter, spring, summer, or fall

  // all you gotta do is call
  shirt_and_pant_type string:64;

  clashes bool;
};
)FIDL")
      .AddFinding("// winter, spring, summer, or fall");

  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, ServiceHubPatternIsDiscouragedPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning(?) Note this is a low-priority check.

  LintTest test;
  test.check_id("service-hub-pattern-is-discouraged")
      .message("")  // TBD
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, StringBoundsNotSpecified) {
  LintTest test;
  test.check_id("string-bounds-not-specified")
      .message("Specify bounds for string")
      .source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_str ${TEST};
};
)FIDL");

  test.substitute("TEST", "string:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string:MAX");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<string:64>:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string");
  ASSERT_FINDINGS(test);

  test.source_template(R"FIDL(
library fidl.a;

const TEST_STRING ${TEST} = "A const str";

)FIDL");

  test.substitute("TEST", "string");
  ASSERT_NO_FINDINGS(test);

  test.source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_str vector<${TEST}>:64;
};
)FIDL");

  test.substitute("TEST", "string:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string:MAX");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string");
  ASSERT_FINDINGS(test);

  test.that("developer cannot work around the check by aliasing").source_template(R"FIDL(
library fidl.a;

alias unbounded_str = ${TEST};

type SomeStruct = struct {
  test_string unbounded_str;
};
)FIDL");

  test.substitute("TEST", "string");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "string:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string:MAX");
  ASSERT_NO_FINDINGS(test);

  test.that("'optional' constraint is not mistaken for size")
      .source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_str ${TEST};
};
)FIDL")
      .substitute("TEST", "string:optional");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "string:<64, optional>");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "string:<MAX, optional>");
  ASSERT_NO_FINDINGS(test);
}

TEST(LintFindingsTests, TodoShouldNotBeDocComment) {
  // Warning on TODO comments.

  std::string source_template = R"FIDL(
library fidl.a;

${TEST1} TODO: Finish the TestStruct declaration
type TestStruct = struct {

  ${TEST2}TODO: Replace the placeholder
  placeholder string:64;${DOC_NOT_ALLOWED_HERE1} TODO(fxbug.dev/FIDL-0000): Add some more fields
};
)FIDL";

  LintTest test;
  test.check_id("todo-should-not-be-doc-comment")
      .message("TODO comment should use a non-flow-through comment marker")
      .source_template(source_template);

  test.substitute({
      {"TEST1", "//"},
      {"TEST2", "//"},
      {"DOC_NOT_ALLOWED_HERE1", "//"},
  });

  ASSERT_NO_FINDINGS(test);

  test.substitute({
                      {"TEST1", "///"},
                      {"TEST2", "//"},
                      {"DOC_NOT_ALLOWED_HERE1", "//"},
                  })
      .suggestion("change '///' to '//'")
      .replacement("//")
      .AddFinding("${TEST1}");

  ASSERT_FINDINGS(test);

  test.substitute({
                      {"TEST1", "//"},
                      {"TEST2", "///"},
                      {"DOC_NOT_ALLOWED_HERE1", "//"},
                  })
      .AddFinding("${TEST2}");

  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.substitute({
                      {"TEST1", "///"},
                      {"TEST2", "///"},
                      {"DOC_NOT_ALLOWED_HERE1", "//"},
                  })
      .AddFinding("${TEST1}")
      .AddFinding("${TEST2}");

  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  // TEST1 and TEST2 as comments and DOC_NOT_ALLOWED_HERE1 as a doc comment leads to a parse error -
  // see ParsingTests.BadTrailingDocCommentInDeclTest.
}

TEST(LintFindingsTests, TooManyNestedLibraries) {
  LintTest test;
  test.check_id("too-many-nested-libraries")
      .message("Avoid library names with more than three dots")
      .source_template(R"FIDL(
library ${TEST};
)FIDL");

  test.substitute("TEST", "fidl.a");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "fuchsia.foo.bar.baz");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "fuchsia.foo.bar.baz.qux");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, UnexpectedTypeForWellKnownBufferConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning on struct, union, and table member name patterns.

  LintTest test;
  test.check_id("unexpected-type-for-well-known-buffer-concept")
      .message(
          "Use fuchsia.mem.Buffer for images and (large) protobufs, when it makes sense to "
          "buffer the data completely")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, UnexpectedTypeForWellKnownBytesConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // (two suggestions) recommend either bytes or array<uint8>. warning on struct, union, and table
  // member name patterns.

  LintTest test;
  test.check_id("unexpected-type-for-well-known-bytes-concept")
      .message("Use bytes or array<uint8> for small non-text data:")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, UnexpectedTypeForWellKnownSocketHandleConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning on struct, union, and table member name patterns.

  LintTest test;
  test.check_id("unexpected-type-for-well-known-socket-handle-concept")
      .message(
          "Use handle<socket> for audio and video streams because data may arrive over "
          "time, or when it makes sense to process data before completely written or "
          "available")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, UnexpectedTypeForWellKnownStringConceptPleaseImplementMe) {
  if (true)
    return;  // disabled pending feature implementation
  // Warning on struct, union, and table members that include certain well-known concepts (like
  // "filename" and "file_name") but their types don't match the type recommended (e.g., string,
  // in this case).

  LintTest test;
  test.check_id("unexpected-type-for-well-known-string-concept")
      .message("Use string for text data:")
      .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

  test.substitute("TEST", "!GOOD_VALUE!");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "!BAD_VALUE!")
      .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
      .replacement("!GOOD_VALUE!");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, VectorBoundsNotSpecified) {
  LintTest test;
  test.check_id("vector-bounds-not-specified")
      .message("Specify bounds for vector")
      .source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_vector ${TEST};
};
)FIDL");

  test.substitute("TEST", "vector<uint8>:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:MAX");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "vector<vector<uint8>:64>");
  ASSERT_FINDINGS(test);

  // Test nested vectors
  test.source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_vector vector<${TEST}>:64;
};
)FIDL");

  test.substitute("TEST", "vector<uint8>:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:MAX");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>");
  ASSERT_FINDINGS(test);

  test.that("developer cannot work around the check by indirect typing using an alias")
      .source_template(R"FIDL(
library fidl.a;

// explanation for why we want this
alias unbounded_vector = ${TEST};

type SomeStruct = struct {
  test_vector unbounded_vector;
};
)FIDL")
      .substitute("TEST", "vector<uint8>");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:64");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:MAX");
  ASSERT_NO_FINDINGS(test);

  test.that("'optional' constraint is not mistaken for size")
      .source_template(R"FIDL(
library fidl.a;

type SomeStruct = struct {
  test_vector ${TEST};
};
)FIDL")
      .substitute("TEST", "vector<uint8>:optional");
  ASSERT_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:<64, optional>");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "vector<uint8>:<MAX, optional>");
  ASSERT_NO_FINDINGS(test);
}

TEST(LintFindingsTests, WrongPrefixForPlatformSourceLibrary) {
  LintTest test;
  test.check_id("wrong-prefix-for-platform-source-library")
      .message("FIDL library name is not currently allowed")
      .source_template(R"FIDL(
library ${TEST}.subcomponent;
)FIDL");

  test.substitute("TEST", "fuchsia");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "fidl");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "test");
  ASSERT_NO_FINDINGS(test);

  test.substitute("TEST", "mylibs")
      .suggestion("change 'mylibs' to fuchsia, perhaps?")
      .replacement("fuchsia, perhaps?");
  ASSERT_FINDINGS(test);
}

TEST(LintFindingsTests, IncludeAndExcludeChecks) {
  LintTest test;
  test.check_id("multiple checks").source_template(R"FIDL(
library ${LIBRARY};

type ${STRUCT_NAME} = struct {
  ${COMMENT_STYLE} TODO: Replace the placeholder
  placeholder string:64;
};
)FIDL");

  test.substitute({
                      {"LIBRARY", "fuchsia.foo.bar.baz.qux"},
                      {"COMMENT_STYLE", "///"},
                      {"STRUCT_NAME", "my_struct"},
                  })
      .AddFinding("too-many-nested-libraries", "Avoid library names with more than three dots",
                  "${LIBRARY}")
      .AddFinding("invalid-case-for-decl-name", "structs must be named in UpperCamelCase",
                  "${STRUCT_NAME}", "change 'my_struct' to 'MyStruct'", "MyStruct")
      .AddFinding("todo-should-not-be-doc-comment",
                  "TODO comment should use a non-flow-through comment marker", "${COMMENT_STYLE}",
                  "change '///' to '//'", "//");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.exclude_checks({
      "too-many-nested-libraries",
      "invalid-case-for-decl-name",
      "todo-should-not-be-doc-comment",
  });
  ASSERT_NO_FINDINGS(test);

  test.exclude_by_default(true);
  ASSERT_NO_FINDINGS(test);

  test.exclude_by_default(true)
      .include_checks({
          "invalid-case-for-decl-name",
      })
      .AddFinding("invalid-case-for-decl-name", "structs must be named in UpperCamelCase",
                  "${STRUCT_NAME}", "change 'my_struct' to 'MyStruct'", "MyStruct");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.exclude_checks({
                          "invalid-case-for-decl-name",
                          "todo-should-not-be-doc-comment",
                      })
      .include_checks({
          "todo-should-not-be-doc-comment",
      })
      .AddFinding("too-many-nested-libraries", "Avoid library names with more than three dots",
                  "${LIBRARY}")
      .AddFinding("todo-should-not-be-doc-comment",
                  "TODO comment should use a non-flow-through comment marker", "${COMMENT_STYLE}",
                  "change '///' to '//'", "//");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.exclude_checks({
                          "invalid-case-for-decl-name",
                          "todo-should-not-be-doc-comment",
                      })
      .AddFinding("too-many-nested-libraries", "Avoid library names with more than three dots",
                  "${LIBRARY}");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.exclude_checks({
                          "invalid-case-for-decl-name",
                          "wrong-prefix-for-platform-source-library",
                          "todo-should-not-be-doc-comment",
                          "vector-bounds-not-specified",
                      })
      .AddFinding("too-many-nested-libraries", "Avoid library names with more than three dots",
                  "${LIBRARY}");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);

  test.exclude_checks({
                          "invalid-case-for-decl-name",
                          "wrong-prefix-for-platform-source-library",
                          "todo-should-not-be-doc-comment",
                          "vector-bounds-not-specified",
                      })
      .excluded_checks_to_confirm({
          "invalid-case-for-decl-name",
          "todo-should-not-be-doc-comment",
      })
      .AddFinding("too-many-nested-libraries", "Avoid library names with more than three dots",
                  "${LIBRARY}");
  ASSERT_FINDINGS_IN_ANY_POSITION(test);
}

}  // namespace

}  // namespace fidl
