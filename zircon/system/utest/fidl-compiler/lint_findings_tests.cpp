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

bool invalid_case_for_enum_member() {
    BEGIN_TEST;

    std::string check_id = "invalid-case-for-enum-member";
    std::string message = "Enum members must be named in ALL_CAPS_SNAKE_CASE";

    std::string bad_enum = "kNegativeOne";
    std::string good_enum = "NEGATIVE_ONE";

    std::string suggestion_description = "change 'kNegativeOne' to 'NEGATIVE_ONE'";
    auto context = suggestion_description.c_str();

    TemplateString source_template(R"FIDL(
library fidl.a;

enum Int8Enum : int8 {
    ${TEST} = -1;
};
)FIDL");

    { // Good test
        auto good = source_template.Substitute({{"TEST", good_enum}});
        TestLibrary library(good);

        Findings findings;
        ASSERT_TRUE(library.Lint(&findings));
    }

    { // Bad test
        auto bad = source_template.Substitute({{"TEST", bad_enum}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_enum, context);
    }
    END_TEST;
}

bool invalid_case_for_constant() {
    BEGIN_TEST;

    std::string check_id = "invalid-case-for-constant";
    std::string message = "Constants must be named in ALL_CAPS_SNAKE_CASE";

    std::string mixed_case_const = "some_CONST";
    std::string k_const = "kSomeConst";
    std::string good_const = "SOME_CONST";

    std::string mixed_case_suggestion = "change 'some_CONST' to 'SOME_CONST'";
    auto mixed_case_context = mixed_case_suggestion.c_str();
    std::string k_const_suggestion = "change 'kSomeConst' to 'SOME_CONST'";
    auto k_const_context = k_const_suggestion.c_str();

    TemplateString source_template(R"FIDL(
library fidl.a;

const uint64 ${TEST} = 1234;
)FIDL");

    { // Good test
        auto good = source_template.Substitute({{"TEST", good_const}});
        TestLibrary library(good);

        Findings findings;
        bool success = library.Lint(&findings);
        ASSERT_TRUE(success);
    }

    { // Bad test mixed_CASE
        auto bad = source_template.Substitute({{"TEST", mixed_case_const}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), mixed_case_context);

        ASSERT_EQ(findings.size(), 1, mixed_case_context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, mixed_case_context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"),
            mixed_case_context);
        ASSERT_STRING_EQ(finding.message(), message, mixed_case_context);
        ASSERT_TRUE(finding.suggestion().has_value(), mixed_case_context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), mixed_case_suggestion,
                         mixed_case_context);
        ASSERT_TRUE(suggestion->replacement().has_value(),
                    mixed_case_context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_const,
                         mixed_case_context);
    }

    { // Bad test kConst
        auto bad = source_template.Substitute({{"TEST", k_const}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), k_const_context);

        ASSERT_EQ(findings.size(), 1, k_const_context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, k_const_context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"),
            k_const_context);
        ASSERT_STRING_EQ(finding.message(), message, k_const_context);
        ASSERT_TRUE(finding.suggestion().has_value(), k_const_context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), k_const_suggestion,
                         k_const_context);
        ASSERT_TRUE(suggestion->replacement().has_value(), k_const_context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_const,
                         k_const_context);
    }
    END_TEST;
}

bool invalid_case_for_protocol() {
    BEGIN_TEST;

    std::string check_id = "invalid-case-for-protocol";
    std::string message = "Protocols must be named in UpperCamelCase";

    std::string bad_protocol = "URLLoader";
    std::string good_protocol = "UrlLoader";

    std::string suggestion_description = "change 'URLLoader' to 'UrlLoader'";
    auto context = suggestion_description.c_str();

    TemplateString source_template(R"FIDL(
library fidl.a;

protocol ${TEST} {};
)FIDL");

    { // Good test
        auto good = source_template.Substitute({{"TEST", good_protocol}});
        TestLibrary library(good);

        Findings findings;
        ASSERT_TRUE(library.Lint(&findings), context);
    }

    { // Bad test
        auto bad = source_template.Substitute({{"TEST", bad_protocol}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_protocol, context);
    }
    END_TEST;
}

bool disallowed_library_name_component() {
    BEGIN_TEST;

    std::string check_id = "disallowed-library-name-component";
    std::string message = "Library names must not contain the following components: common, service, util, base, f<letter>l, zx<word>";

    std::string bad_library_name_component = "zxsocket";
    std::string good_library_name_component = "display";

    auto context = "Test for bad library name: zxsocket";

    TemplateString source_template(R"FIDL(
library fidl.${TEST};
)FIDL");

    { // Good test
        auto good = source_template.Substitute({{"TEST", "display"}});
        TestLibrary library(good);

        Findings findings;
        ASSERT_TRUE(library.Lint(&findings));
    }

    { // Bad test: zx<word>
        auto bad = source_template.Substitute({{"TEST", "zxsocket"}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_FALSE(finding.suggestion().has_value(), context);
    }

    { // Bad test: f<letter>l
        auto bad = source_template.Substitute({{"TEST", "ful"}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_FALSE(finding.suggestion().has_value(), context);
    }

    { // Bad test: banned word like "common"
        auto bad = source_template.Substitute({{"TEST", "common"}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_FALSE(finding.suggestion().has_value(), context);
    }
    END_TEST;
}

bool invalid_case_for_primitive_alias() {
    BEGIN_TEST;

    std::string check_id = "invalid-case-for-primitive-alias";
    std::string message = "Primitive aliases must be named in lower_snake_case";

    std::string bad_alias = "WhatIfSomeoneDoes_This";
    std::string good_alias = "what_if_someone_does_this";

    std::string suggestion_description = "change 'WhatIfSomeoneDoes_This' to 'what_if_someone_does_this'";
    auto context = suggestion_description.c_str();

    TemplateString source_template(R"FIDL(
library fidl.a;

using foo as ${TEST};
using bar as baz;
)FIDL");

    { // Good test
        auto good = source_template.Substitute({{"TEST", good_alias}});
        Findings findings;
        TestLibrary library(good);
        ASSERT_TRUE(library.Lint(&findings));
    }

    { // Bad test
        auto bad = source_template.Substitute({{"TEST", bad_alias}});
        Findings findings;
        TestLibrary library(bad);
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_alias, context);
    }
    END_TEST;
}

bool wrong_prefix_for_platform_source_library() {
    BEGIN_TEST;

    std::string check_id = "wrong-prefix-for-platform-source-library";
    std::string message = "FIDL library name is not currently allowed";

    std::string bad_prefix = "mylibs";

    std::string suggestion_description = "change 'mylibs' to fuchsia, perhaps?";
    auto context = suggestion_description.c_str();

    TemplateString source_template(R"FIDL(
library ${TEST}.subcomponent;
)FIDL");

    { // Good tests
        Findings findings;
        ASSERT_TRUE(
            TestLibrary(source_template.Substitute({{"TEST", "fuchsia"}}))
                .Lint(&findings));

        ASSERT_TRUE(
            TestLibrary(source_template.Substitute({{"TEST", "fidl"}}))
                .Lint(&findings));

        ASSERT_TRUE(
            TestLibrary(source_template.Substitute({{"TEST", "test"}}))
                .Lint(&findings));
    }

    { // Bad test
        auto bad = source_template.Substitute({{"TEST", bad_prefix}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(source_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), "fuchsia, perhaps?",
                         context);
    }

    END_TEST;
}

bool invalid_case_for_decl_member() {
    BEGIN_TEST;

    std::string check_id = "invalid-case-for-decl-member";
    std::string message = "Structs, unions, and tables members must be named in lower_snake_case";

    std::string bad_member = "agentRequestCount";
    std::string good_member = "agent_request_count";

    std::string suggestion_description = "change 'agentRequestCount' to 'agent_request_count'";
    auto context = suggestion_description.c_str();

    TemplateString struct_template(R"FIDL(
library fidl.a;

struct S {
    string ${TEST};
    int32 i;
};
)FIDL");

    TemplateString union_template(R"FIDL(
library fidl.a;

union U {
    int32 i;
    float32 ${TEST};
    string s;
};
)FIDL");

    TemplateString xunion_template(R"FIDL(
library fidl.a;

xunion Xu {
    int32 i;
    float32 f;
    string s;
    vector<int64> ${TEST};
};
)FIDL");

    TemplateString table_template(R"FIDL(
library fidl.a;

table T {
    1: string ${TEST};
    2: int64 i;
};
)FIDL");

    { // Good test
        Findings findings;

        ASSERT_TRUE(
            TestLibrary(struct_template.Substitute({{"TEST", good_member}}))
                .Lint(&findings));

        ASSERT_TRUE(
            TestLibrary(union_template.Substitute({{"TEST", good_member}}))
                .Lint(&findings));

        ASSERT_TRUE(
            TestLibrary(xunion_template.Substitute({{"TEST", good_member}}))
                .Lint(&findings));

        ASSERT_TRUE(
            TestLibrary(table_template.Substitute({{"TEST", good_member}}))
                .Lint(&findings));
    }

    { // Bad test: struct
        auto bad = struct_template.Substitute({{"TEST", bad_member}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(struct_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_member, context);
    }

    { // Bad test: union
        auto bad = union_template.Substitute({{"TEST", bad_member}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(union_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_member, context);
    }

    { // Bad test: xunion
        auto bad = xunion_template.Substitute({{"TEST", bad_member}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(xunion_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_member, context);
    }

    { // Bad test: table
        auto bad = table_template.Substitute({{"TEST", bad_member}});
        TestLibrary library(bad);

        Findings findings;
        ASSERT_FALSE(library.Lint(&findings), context);

        ASSERT_EQ(findings.size(), 1, context);
        auto& finding = findings.front();
        ASSERT_STRING_EQ(finding.subcategory(), check_id, context);
        ASSERT_STRING_EQ(
            finding.source_location().position(),
            library.FileLocation(table_template.str(), "${TEST}"), context);
        ASSERT_STRING_EQ(finding.message(), message, context);
        ASSERT_TRUE(finding.suggestion().has_value(), context);
        auto& suggestion = finding.suggestion();
        ASSERT_STRING_EQ(suggestion->description(), suggestion_description,
                         context);
        ASSERT_TRUE(suggestion->replacement().has_value(), context);
        ASSERT_STRING_EQ(*suggestion->replacement(), good_member, context);
    }
    END_TEST;
}

BEGIN_TEST_CASE(lint_findings_tests)

RUN_TEST(invalid_case_for_enum_member)
RUN_TEST(invalid_case_for_constant)
RUN_TEST(invalid_case_for_protocol)
RUN_TEST(wrong_prefix_for_platform_source_library)
RUN_TEST(disallowed_library_name_component)
RUN_TEST(invalid_case_for_primitive_alias)
RUN_TEST(invalid_case_for_decl_member)

END_TEST_CASE(lint_findings_tests)

} // namespace

} // namespace fidl
