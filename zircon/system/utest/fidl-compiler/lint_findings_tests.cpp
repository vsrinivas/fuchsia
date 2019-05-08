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

bool invalid_case_for_constant_checking_bitfield_member_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Implement the check, then
    // UNCOMMENT BITFIELD CONSTANT CHECK TESTS IN:
    //   invalid_case_for_constant()
    // And remove this test function.

    END_TEST;
}

bool invalid_case_for_constant() {
    BEGIN_TEST;

    std::map<std::string, std::string> named_templates = {
        {"Constants", R"FIDL(
library fidl.a;

const uint64 ${TEST} = 1234;
)FIDL"},
        {"enum members", R"FIDL(
library fidl.a;

enum Int8Enum : int8 {
    ${TEST} = -1;
};
)FIDL"},
        //         {"Bitfield members", R"FIDL(  // CHECK NOT YET IMPLEMENTED!
        // library fidl.a;

        // bits Uint32Bitfield : uint32 {
        //   ${TEST} = 0x00000004;
        // };
        // )FIDL"},
    };

    for (auto const& named_template : named_templates) {
        LintTest test;
        test.check_id("invalid-case-for-constant")
            .message(named_template.first + " must be named in ALL_CAPS_SNAKE_CASE")
            .source_template(named_template.second);

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
    }

    END_TEST;
}

bool invalid_case_for_decl_name() {
    BEGIN_TEST;

    std::map<std::string, std::string> named_templates = {
        {"Protocols", R"FIDL(
library fidl.a;

protocol ${TEST} {};
)FIDL"},
        {"Enums", R"FIDL(
library fidl.a;

enum ${TEST} : int8 {
    SOME_CONST = -1;
};
)FIDL"},
        {"Bitfields", R"FIDL(
library fidl.a;

bits ${TEST} : uint32 {
  SOME_BIT = 0x00000004;
};
)FIDL"},
        {"Structs", R"FIDL(
library fidl.a;

struct ${TEST} {
    string decl_member;
};
)FIDL"},
        {"Tables", R"FIDL(
library fidl.a;

table ${TEST} {
    1: string decl_member;
};
)FIDL"},
        {"Unions", R"FIDL(
library fidl.a;

union ${TEST} {
    string decl_member;
};
)FIDL"},
        {"XUnions", R"FIDL(
library fidl.a;

xunion ${TEST} {
    string decl_member;
};
)FIDL"},
    };

    for (auto const& named_template : named_templates) {
        LintTest test;
        test.check_id("invalid-case-for-decl-name")
            .message(named_template.first + " must be named in UpperCamelCase")
            .source_template(named_template.second);

        test.substitute("TEST", "UrlLoader");
        ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

        test.substitute("TEST", "URLLoader")
            .suggestion("change 'URLLoader' to 'UrlLoader'")
            .replacement("UrlLoader");
        ASSERT_TRUE(test.ExpectOneFinding(), "Failed");
    }

    END_TEST;
}

bool invalid_case_for_decl_member_checking_method_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Implement the check and then add test templates to
    //   invalid_case_for_decl_member()
    // And remove this test function.

    END_TEST;
}

bool invalid_case_for_decl_member_checking_parameter_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Implement the check and then add test templates to
    //   invalid_case_for_decl_member()
    // And remove this test function.

    END_TEST;
}

bool invalid_case_for_decl_member() {
    BEGIN_TEST;

    std::map<std::string, std::string> named_templates = {
        {"struct", R"FIDL(
library fidl.a;

struct DeclName {
    string ${TEST};
};
)FIDL"},
        {"table", R"FIDL(
library fidl.a;

table DeclName {
    1: string ${TEST};
};
)FIDL"},
        {"union", R"FIDL(
library fidl.a;

union DeclName {
    string ${TEST};
};
)FIDL"},
        {"xunion", R"FIDL(
library fidl.a;

xunion DeclName {
    string ${TEST};
};
)FIDL"},
    };

    for (auto const& named_template : named_templates) {
        LintTest test;
        test.check_id("invalid-case-for-decl-member")
            .message(named_template.first + " members must be named in lower_snake_case")
            .source_template(named_template.second);

        test.substitute("TEST", "agent_request_count");
        ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

        test.substitute("TEST", "agentRequestCount")
            .suggestion("change 'agentRequestCount' to 'agent_request_count'")
            .replacement("agent_request_count");
        ASSERT_TRUE(test.ExpectOneFinding(), "Failed");
    }

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

bool disallowed_library_name_component() {
    BEGIN_TEST;

    LintTest test;
    test.check_id("disallowed-library-name-component")
        .message("Library names must not contain the following components: common, service, util, "
                 "base, f<letter>l, zx<word>")
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

bool no_commonly_reserved_words_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("no-commonly-reserved-words")
        .message("Avoid commonly reserved words")
        .source_template(R"FIDL(
library fidl.a;

using foo as ${TEST};
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
        "interface",
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
        ASSERT_TRUE(test.ExpectOneFinding(), "Failed");
    }

    END_TEST;
}

bool too_many_nested_libraries_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("too-many-nested-libraries")
        .message("Avoid library names with more than two dots")
        .source_template(R"FIDL(
library ${TEST};
)FIDL");

    test.substitute("TEST", "fidl.a");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "fuchsia.foo.bar.baz");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool primitive_alias_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("primitive-alias-name-repeats-library-name")
        .message("Primitive aliases must not repeat names from the enclosing library")
        .source_template(R"FIDL(
library fidl.bandwidth;

using foo as ${TEST};
)FIDL");

    test.substitute("TEST", "rate");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "bandwidth");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool constant_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("constant-name-repeats-library-name")
        .message("Constant names must not repeat names from the enclosing library")
        .source_template(R"FIDL(
library fidl.bandwidth;

const uint64 ${TEST} = 1234;
)FIDL");

    test.substitute("TEST", "RATE");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "BANDWIDTH");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool constant_should_use_common_prefix_suffix_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning for "MINIMUM_..." or "MAXIMUM...", or maybe(?) "..._CAP" Also for instance
    // "SET_CLIENT_NAME_MAX_LEN" -> "MAX_CLIENT_NAME_LEN" or MAX_LEN_CLIENT_NAME", so detect
    // "_MAX" or "_MIN" as separate words in middle or at end of identifier.

    LintTest test;
    test.check_id("constant-should-use-common-prefix-suffix")
        .message("Constants should use the standard prefix and/or suffix for common concept, "
                 "such as MIN and MAX, rather than MINIMUM and MAXIMUM, respectively.")
        .source_template(R"FIDL(
library fidl.a;

const uint64 ${TEST} = 1234;
)FIDL");

    test.substitute("TEST", "MIN_HEIGHT");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "MAX_HEIGHT");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "NAME_MIN_LEN");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "NAME_MAX_LEN");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    // Not yet determined if the standard should be LEN or LENGTH, or both
    // test.substitute("TEST", "BYTES_LEN");
    // ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "THRESHOLD_MIN");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "THRESHOLD_MAX");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "MINIMUM_HEIGHT")
        .suggestion("change 'MINIMUM_HEIGHT' to 'MIN_HEIGHT'")
        .replacement("MIN_HEIGHT");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "MAXIMUM_HEIGHT")
        .suggestion("change 'MAXIMUM_HEIGHT' to 'MAX_HEIGHT'")
        .replacement("MAX_HEIGHT");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "NAME_MINIMUM_LEN")
        .suggestion("change 'NAME_MINIMUM_LEN' to 'NAME_MIN_LEN'")
        .replacement("NAME_MIN_LEN");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "NAME_MAXIMUM_LEN")
        .suggestion("change 'NAME_MAXIMUM_LEN' to 'NAME_MAX_LEN'")
        .replacement("NAME_MAX_LEN");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    // Not yet determined if the standard should be LEN or LENGTH, or both
    // test.substitute("TEST", "BYTES_LENGTH")
    //     .suggestion("change 'BYTES_LENGTH' to 'BYTES_LEN'")
    //     .replacement("BYTES_LEN");
    // ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "THRESHOLD_MINIMUM")
        .suggestion("change 'THRESHOLD_MINIMUM' to 'THRESHOLD_MIN'")
        .replacement("THRESHOLD_MIN");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "THRESHOLD_MAXIMUM")
        .suggestion("change 'THRESHOLD_MAXIMUM' to 'THRESHOLD_MAX'")
        .replacement("THRESHOLD_MAX");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    test.substitute("TEST", "THRESHOLD_CAP")
        .suggestion("change 'THRESHOLD_CAP' to 'THRESHOLD_MAX'")
        .replacement("THRESHOLD_MAX");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool manager_protocols_are_discouraged_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("manager-protocols-are-discouraged")
        .message("The name Manager may be used as a name of last resort for a protocol with broad "
                 "scope")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool disallowed_protocol_name_ends_in_service_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Error if ends in "Service", warning if includes "Service" as a word, but "Serviceability"
    // ("Service" is only part of a word) is OK.

    LintTest test;
    test.check_id("disallowed-protocol-name-ends-in-service")
        .message("Protocols must not include the name 'service.'")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool disallowed_protocol_name_includes_service_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Error if ends in "Service", warning if includes "Service" as a word, but "Serviceability"
    // ("Service" is only part of a word) is OK.

    LintTest test;
    test.check_id("disallowed-protocol-name-includes-service")
        .message("Protocols must not include the name 'service.'")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool method_name_repeats_protocol_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("method-name-repeats-protocol-name")
        .message("Method names must not repeat names from the enclosing protocol")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool method_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("method-name-repeats-library-name")
        .message("Method names must not repeat names from the enclosing protocol (or library)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool member_name_repeats_enclosing_type_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("member-name-repeats-enclosing-type-name")
        .message("Member names must not repeat names from the enclosing type")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool member_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("member-name-repeats-library-name")
        .message("Member names must not repeat names from the enclosing type (or library)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool enum_member_name_repeats_enclosing_type_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("enum-member-name-repeats-enclosing-type-name")
        .message("Enum member names must not repeat names from the enclosing type")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool enum_member_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("enum-member-name-repeats-library-name")
        .message("Enum member names must not repeat names from the enclosing type (or library)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool bitfield_name_repeats_enclosing_type_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("bitfield-name-repeats-enclosing-type-name")
        .message("Bitfield names must not repeat names from the enclosing type")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool bitfield_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("bitfield-name-repeats-library-name")
        .message("Bitfield names must not repeat names from the enclosing type (or library)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool bitfield_member_name_repeats_enclosing_type_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("bitfield-member-name-repeats-enclosing-type-name")
        .message("Bitfield members must not repeat names from the enclosing type")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool bitfield_member_name_repeats_library_name_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("bitfield-member-name-repeats-library-name")
        .message("Bitfield members must not repeat names from the enclosing type (or library)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool incorrect_line_indent_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Use formatter: It can handle all checks in this section ("Syntax"), but linter can run the
    // formatter and diff results to see if the file needs to be formatted.

    LintTest test;
    test.check_id("incorrect-line-indent")
        .message("Use 4 space indents")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool tabs_disallowed_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Use formatter.

    LintTest test;
    test.check_id("tabs-disallowed")
        .message("Never use tabs")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool trailing_whitespace_disallowed_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Use formatter.

    LintTest test;
    test.check_id("trailing-whitespace-disallowed")
        .message("Avoid trailing whitespace")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool incorrect_spacing_between_declarations_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Use formatter.

    LintTest test;
    test.check_id("incorrect-spacing-between-declarations")
        .message("Separate declarations for struct, union, enum, and protocol constructs from "
                 "other declarations with one blank line (two consecutive newline characters)")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool end_of_file_should_be_one_newline_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Use formatter.
    //
    // Warning: often hard to get right depending on the editor.

    LintTest test;
    test.check_id("end-of-file-should-be-one-newline")
        .message("End files with exactly one newline character")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool note_comment_should_not_flow_through_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning on NOTE comments.

    LintTest test;
    test.check_id("note-comment-should-not-flow-through")
        .message("NOTE comments should not flow through")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool deprecation_comment_should_not_flow_through_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning on DEPRECATED comments.

    LintTest test;
    test.check_id("deprecation-comment-should-not-flow-through")
        .message("DEPRECATED comments should not flow through")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool todo_comment_should_not_flow_through_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning on TODO comments.

    LintTest test;
    test.check_id("todo-comment-should-not-flow-through")
        .message("TODO comments should not flow through")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool copyright_notice_should_not_flow_through_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("copyright-notice-should-not-flow-through")
        .message("Copyright notice should not flow through")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool discontiguous_comment_block_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("discontiguous-comment-block")
        .message("Multi-line comments should be in a single contiguous comment block, with all "
                 "lines prefixed by comment markers")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool invalid_copyright_for_platform_source_library_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("invalid-copyright-for-platform-source-library")
        .message("FIDL files defined in the Platform Source Tree (i.e., defined in "
                 "fuchsia.googlesource.com) must begin with the standard copyright notice")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool library_name_does_not_match_file_path_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("library-name-does-not-match-file-path")
        .message("The <library> directory is named using the dot-separated name of the FIDL "
                 "library")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool inconsistent_type_for_recurring_file_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

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
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool inconsistent_type_for_recurring_library_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

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
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool string_bounds_not_specified_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("string-bounds-not-specified")
        .message("Specify bounds for string")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool vector_bounds_not_specified_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    LintTest test;
    test.check_id("vector-bounds-not-specified")
        .message("Specify bounds for vector")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool method_return_status_missing_ok_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

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
        .message("") // TBD
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool method_returns_status_with_non_optional_result_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning if return a status and a non-optional result? we now have another more expressive
    // pattern for this, this section should be updated. Specifically, see:
    // https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/fidl/reference/ftp/ftp-014.md.

    LintTest test;
    test.check_id("method-returns-status-with-non-optional-result")
        .message("") // TBD
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool unexpected_type_for_well_known_string_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

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
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool unexpected_type_for_well_known_bytes_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

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
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool unexpected_type_for_well_known_buffer_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning on struct, union, and table member name patterns.

    LintTest test;
    test.check_id("unexpected-type-for-well-known-buffer-concept")
        .message("Use fuchsia.mem.Buffer for images and (large) protobufs, when it makes sense to "
                 "buffer the data completely")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool unexpected_type_for_well_known_socket_handle_concept_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning on struct, union, and table member name patterns.

    LintTest test;
    test.check_id("unexpected-type-for-well-known-socket-handle-concept")
        .message("Use handle<socket> for audio and video streams because data may arrive over "
                 "time, or when it makes sense to process data before completely written or "
                 "available")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool method_should_pipeline_protocols_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Error(?) if the return tuple contains one value of another FIDL protocol type. Returning a
    // protocol is better done by sending a request for pipelining. This will be hard to lint at the
    // raw level, because you do not know to differentiate Bar from a protocol vs a message vs a bad
    // name since resolution is done later. This may call for linting to be done on the JSON IR.

    LintTest test;
    test.check_id("method-should-pipeline-protocols")
        .message("") // TBD
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool service_hub_pattern_is_discouraged_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning(?) Note this is a low-priority check.

    LintTest test;
    test.check_id("service-hub-pattern-is-discouraged")
        .message("") // TBD
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool excessive_number_of_separate_protocols_for_library_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Or if a directory of fidl files contains more than some tolerance number of files AND any
    // fidl file(s) in that directory contains more than some smaller cap number of protocols per
    // fidl file. The fuchsia.ledger would be a good one to look at since it defines many protocols.
    // We do not have public vs private visibility yet, and the cap may only be needed for public
    // things.

    LintTest test;
    test.check_id("excessive-number-of-separate-protocols-for-library")
        .message("Some libraries create separate protocol instances for every logical object in "
                 "the protocol, but this approach has a number of disadvantages:")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

bool excessive_number_of_separate_protocols_for_file_please_implement_me() {
    if (true)
        return true; // disabled pending feature implementation
    BEGIN_TEST;

    // Warning(?) if a fidl file contains more than some tolerance cap number of protocols.
    //
    // Or if a directory of fidl files contains more than some tolerance number of files AND any
    // fidl file(s) in that directory contains more than some smaller cap number of protocols per
    // fidl file. The fuchsia.ledger would be a good one to look at since it defines many protocols.
    // We do not have public vs private visibility yet, and the cap may only be needed for public
    // things.

    LintTest test;
    test.check_id("excessive-number-of-separate-protocols-for-file")
        .message("Some libraries create separate protocol instances for every logical object in "
                 "the protocol, but this approach has a number of disadvantages:")
        .source_template(R"FIDL(
library fidl.a;

PUT FIDL CONTENT HERE WITH PLACEHOLDERS LIKE:
    ${TEST}
TO SUBSTITUTE WITH GOOD_VALUE AND BAD_VALUE CASES.
)FIDL");

    test.substitute("TEST", "!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectNoFinding(), "Failed");

    test.substitute("TEST", "!BAD_VALUE!")
        .suggestion("change '!BAD_VALUE!' to '!GOOD_VALUE!'")
        .replacement("!GOOD_VALUE!");
    ASSERT_TRUE(test.ExpectOneFinding(), "Failed");

    END_TEST;
}

BEGIN_TEST_CASE(lint_findings_tests)

RUN_TEST(bitfield_member_name_repeats_enclosing_type_name_please_implement_me)
RUN_TEST(bitfield_member_name_repeats_library_name_please_implement_me)
RUN_TEST(bitfield_name_repeats_enclosing_type_name_please_implement_me)
RUN_TEST(bitfield_name_repeats_library_name_please_implement_me)
RUN_TEST(constant_name_repeats_library_name_please_implement_me)
RUN_TEST(constant_should_use_common_prefix_suffix_please_implement_me)
RUN_TEST(copyright_notice_should_not_flow_through_please_implement_me)
RUN_TEST(deprecation_comment_should_not_flow_through_please_implement_me)
RUN_TEST(disallowed_library_name_component)
RUN_TEST(disallowed_protocol_name_ends_in_service_please_implement_me)
RUN_TEST(disallowed_protocol_name_includes_service_please_implement_me)
RUN_TEST(discontiguous_comment_block_please_implement_me)
RUN_TEST(end_of_file_should_be_one_newline_please_implement_me)
RUN_TEST(enum_member_name_repeats_enclosing_type_name_please_implement_me)
RUN_TEST(enum_member_name_repeats_library_name_please_implement_me)
RUN_TEST(excessive_number_of_separate_protocols_for_file_please_implement_me)
RUN_TEST(excessive_number_of_separate_protocols_for_library_please_implement_me)
RUN_TEST(inconsistent_type_for_recurring_file_concept_please_implement_me)
RUN_TEST(inconsistent_type_for_recurring_library_concept_please_implement_me)
RUN_TEST(incorrect_line_indent_please_implement_me)
RUN_TEST(incorrect_spacing_between_declarations_please_implement_me)
RUN_TEST(invalid_case_for_constant)
RUN_TEST(invalid_case_for_constant_checking_bitfield_member_please_implement_me) // TO MERGE
RUN_TEST(invalid_case_for_decl_member)
RUN_TEST(invalid_case_for_decl_member_checking_method_please_implement_me)    // TO MERGE
RUN_TEST(invalid_case_for_decl_member_checking_parameter_please_implement_me) // TO MERGE
RUN_TEST(invalid_case_for_decl_name)
RUN_TEST(invalid_case_for_primitive_alias)
RUN_TEST(invalid_copyright_for_platform_source_library_please_implement_me)
RUN_TEST(library_name_does_not_match_file_path_please_implement_me)
RUN_TEST(manager_protocols_are_discouraged_please_implement_me)
RUN_TEST(member_name_repeats_enclosing_type_name_please_implement_me)
RUN_TEST(member_name_repeats_library_name_please_implement_me)
RUN_TEST(method_name_repeats_library_name_please_implement_me)
RUN_TEST(method_name_repeats_protocol_name_please_implement_me)
RUN_TEST(method_return_status_missing_ok_please_implement_me)
RUN_TEST(method_returns_status_with_non_optional_result_please_implement_me)
RUN_TEST(method_should_pipeline_protocols_please_implement_me)
RUN_TEST(no_commonly_reserved_words_please_implement_me)
RUN_TEST(note_comment_should_not_flow_through_please_implement_me)
RUN_TEST(primitive_alias_name_repeats_library_name_please_implement_me)
RUN_TEST(service_hub_pattern_is_discouraged_please_implement_me)
RUN_TEST(string_bounds_not_specified_please_implement_me)
RUN_TEST(tabs_disallowed_please_implement_me)
RUN_TEST(todo_comment_should_not_flow_through_please_implement_me)
RUN_TEST(too_many_nested_libraries_please_implement_me)
RUN_TEST(trailing_whitespace_disallowed_please_implement_me)
RUN_TEST(unexpected_type_for_well_known_buffer_concept_please_implement_me)
RUN_TEST(unexpected_type_for_well_known_bytes_concept_please_implement_me)
RUN_TEST(unexpected_type_for_well_known_socket_handle_concept_please_implement_me)
RUN_TEST(unexpected_type_for_well_known_string_concept_please_implement_me)
RUN_TEST(vector_bounds_not_specified_please_implement_me)
RUN_TEST(wrong_prefix_for_platform_source_library)

END_TEST_CASE(lint_findings_tests)

} // namespace

} // namespace fidl
