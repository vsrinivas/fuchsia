// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"
#include "unittest_helpers.h"

#include <sstream>

#include <fidl/findings.h>
#include <fidl/template_string.h>
#include <fidl/utils.h>

namespace fidl {

namespace {

class LintTest {

public:
    LintTest& check_id(std::string check_id) {
        check_id_ = check_id;
        return *this;
    }

    LintTest& message(std::string message) {
        message_ = message;
        return *this;
    }

    LintTest& suggestion(std::string suggestion) {
        suggestion_ = suggestion;
        return *this;
    }

    LintTest& replacement(std::string replacement) {
        replacement_ = replacement;
        return *this;
    }

    LintTest& source_template(std::string template_str) {
        source_template_ = TemplateString(template_str);
        return *this;
    }

    LintTest& substitute(Substitutions substitutions) {
        substitutions_ = substitutions;
        return *this;
    }

    // Shorthand for the common occurrence of a single substitution variable.
    LintTest& substitute(std::string var_name, std::string value) {
        return substitute({{var_name, value}});
    }

    bool ExpectNoFinding() {
        ASSERT_TRUE(ValidTest(), "Bad test!");
        auto source = source_template_.Substitute(substitutions_);
        TestLibrary library(source);
        Findings findings;
        bool passed = library.Lint(&findings);
        if (!passed) {
            auto& finding = findings.front();
            std::string context = source;
            context.append("\n");
            context.append(finding.subcategory());
            context.append("\n");
            if (finding.source_location().data().size() > 0) {
                // Note: Parser error (pre-lint) includes its own position
                context.append(finding.source_location().position());
                context.append("\n");
            }
            context.append(finding.message());
            ASSERT_TRUE(passed, context.c_str());
        }
        return true;
    }

    bool ExpectOneFinding() {
        ASSERT_TRUE(ValidTest(), "Bad test!");
        auto source = source_template_.Substitute(substitutions_);
        auto context = source.c_str();

        TestLibrary library(source);
        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id_, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template_.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message_, context);
        if (!suggestion_.has_value()) {
            ASSERT_FALSE(finding.suggestion().has_value(), context);
        } else {
            ASSERT_TRUE(finding.suggestion().has_value(), context);
            auto& suggestion = finding.suggestion();
            ASSERT_STRING_EQ(suggestion->description(),
                             suggestion_.value(), context);
            if (!replacement_.has_value()) {
                ASSERT_FALSE(suggestion->replacement().has_value(), context);
            } else {
                ASSERT_TRUE(suggestion->replacement().has_value(), context);
                ASSERT_STRING_EQ(*suggestion->replacement(),
                                 replacement_.value(), context);
            }
        }

        return true;
    }

private:
    bool ValidTest() {
        ASSERT_FALSE(check_id_.size() == 0, "Missing check_id");
        ASSERT_FALSE(message_.size() == 0, "Missing message");
        ASSERT_FALSE(source_template_.str().size() == 0,
                     "Missing source template");
        ASSERT_FALSE(source_template_.Substitute(substitutions_, false) !=
                         source_template_.Substitute(substitutions_, true),
                     "Missing template substitutions");
        return true;
    }

    std::string check_id_;
    std::string message_;
    std::optional<std::string> suggestion_;
    std::optional<std::string> replacement_;
    TemplateString source_template_;
    Substitutions substitutions_;
};

bool invalid_case_for_enum_member() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-enum-member")
        .message("Enum members must be named in ALL_CAPS_SNAKE_CASE")
        .source_template(R"FIDL(
library fidl.a;

enum Int8Enum : int8 {
    ${TEST} = -1;
};
)FIDL");

    test.substitute("TEST", "NEGATIVE_ONE");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "kNegativeOne")
        .suggestion("change 'kNegativeOne' to 'NEGATIVE_ONE'")
        .replacement("NEGATIVE_ONE");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_constant() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-constant")
        .message("Constants must be named in ALL_CAPS_SNAKE_CASE")
        .source_template(R"FIDL(
library fidl.a;

const uint64 ${TEST} = 1234;
)FIDL");

    test.substitute("TEST", "SOME_CONST");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "some_CONST")
        .suggestion("change 'some_CONST' to 'SOME_CONST'")
        .replacement("SOME_CONST");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "kSomeConst")
        .suggestion("change 'kSomeConst' to 'SOME_CONST'")
        .replacement("SOME_CONST");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_protocol() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-protocol")
        .message("Protocols must be named in UpperCamelCase")
        .source_template(R"FIDL(
library fidl.a;

protocol ${TEST} {};
)FIDL");

    test.substitute("TEST", "UrlLoader");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "URLLoader")
        .suggestion("change 'URLLoader' to 'UrlLoader'")
        .replacement("UrlLoader");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool disallowed_library_name_component() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("disallowed-library-name-component")
        .message("Library names must not contain the following components: common, service, util, base, f<letter>l, zx<word>")
        .source_template(R"FIDL(
library fidl.${TEST};
)FIDL");

    test.substitute("TEST", "display");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    // Bad test: zx<word>
    test.substitute("TEST", "zxsocket");
    // no suggestion
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    // Bad test: f<letter>l
    test.substitute("TEST", "ful");
    // no suggestion
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    // Bad test: banned words like "common"
    test.substitute("TEST", "common");
    // no suggestion
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_primitive_alias() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-primitive-alias")
        .message("Primitive aliases must be named in lower_snake_case")
        .source_template(R"FIDL(
library fidl.a;

using foo as ${TEST};
using bar as baz;
)FIDL");

    test.substitute("TEST", "what_if_someone_does_this");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "WhatIfSomeoneDoes_This")
        .suggestion("change 'WhatIfSomeoneDoes_This' to 'what_if_someone_does_this'")
        .replacement("what_if_someone_does_this");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool wrong_prefix_for_platform_source_library() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("wrong-prefix-for-platform-source-library")
        .message("FIDL library name is not currently allowed")
        .source_template(R"FIDL(
library ${TEST}.subcomponent;
)FIDL");

    test.substitute("TEST", "fuchsia");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "fidl");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "test");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "mylibs")
        .suggestion("change 'mylibs' to fuchsia, perhaps?")
        .replacement("fuchsia, perhaps?");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_struct_member() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-decl-member")
        .message("Structs, unions, and tables members must be named in lower_snake_case")
        .source_template(R"FIDL(
library fidl.a;

struct S {
    string ${TEST};
    int32 i;
};
)FIDL");

    test.substitute("TEST", "agent_request_count");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "agentRequestCount")
        .suggestion("change 'agentRequestCount' to 'agent_request_count'")
        .replacement("agent_request_count");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_union_member() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-decl-member")
        .message("Structs, unions, and tables members must be named in lower_snake_case")
        .source_template(R"FIDL(
library fidl.a;

union U {
    int32 i;
    float32 ${TEST};
    string s;
};
)FIDL");

    test.substitute("TEST", "agent_request_count");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "agentRequestCount")
        .suggestion("change 'agentRequestCount' to 'agent_request_count'")
        .replacement("agent_request_count");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_xunion_member() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-decl-member")
        .message("Structs, unions, and tables members must be named in lower_snake_case")
        .source_template(R"FIDL(
library fidl.a;

xunion Xu {
    int32 i;
    float32 f;
    string s;
    vector<int64> ${TEST};
};
)FIDL");

    test.substitute("TEST", "agent_request_count");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "agentRequestCount")
        .suggestion("change 'agentRequestCount' to 'agent_request_count'")
        .replacement("agent_request_count");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_case_for_table_member() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-case-for-decl-member")
        .message("Structs, unions, and tables members must be named in lower_snake_case")
        .source_template(R"FIDL(
library fidl.a;

table T {
    1: string ${TEST};
    2: int64 i;
};
)FIDL");

    test.substitute("TEST", "agent_request_count");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "agentRequestCount")
        .suggestion("change 'agentRequestCount' to 'agent_request_count'")
        .replacement("agent_request_count");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

BEGIN_TEST_CASE(lint_findings_tests)

RUN_TEST(invalid_case_for_enum_member)
RUN_TEST(invalid_case_for_constant)
RUN_TEST(invalid_case_for_protocol)
RUN_TEST(wrong_prefix_for_platform_source_library)
RUN_TEST(disallowed_library_name_component)
RUN_TEST(invalid_case_for_primitive_alias)
RUN_TEST(invalid_case_for_struct_member)
RUN_TEST(invalid_case_for_union_member)
RUN_TEST(invalid_case_for_xunion_member)
RUN_TEST(invalid_case_for_table_member)

END_TEST_CASE(lint_findings_tests)

} // namespace

} // namespace fidl
