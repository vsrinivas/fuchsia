// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unittest_helpers.h"

#include <fidl/utils.h>
#include <lib/fit/function.h>

namespace fidl {
namespace utils {

namespace {

bool compare_id_to_words(std::string id, std::string lowercase_words_string) {
    std::ostringstream ss;
    for (auto word : id_to_words(id)) {
        if (ss.tellp() > 0) {
            ss << " ";
        }
        ss << word;
    }
    ASSERT_STRING_EQ(ss.str(), lowercase_words_string, std::string("Failed for " + id).c_str());
    return true;
}

bool id_to_words() {
    BEGIN_TEST;

    compare_id_to_words("agent_request_count",
                        "agent request count");
    compare_id_to_words("common",
                        "common");
    compare_id_to_words("Service",
                        "service");
    compare_id_to_words("Blink32",
                        "blink32");
    compare_id_to_words("the21jumpStreet",
                        "the21jump street");
    compare_id_to_words("the21JumpStreet",
                        "the21 jump street");
    compare_id_to_words("onOntologyUpdate",
                        "on ontology update");
    compare_id_to_words("urlLoader",
                        "url loader");
    compare_id_to_words("onUrlLoader",
                        "on url loader");
    compare_id_to_words("OnOntologyUpdate",
                        "on ontology update");
    compare_id_to_words("UrlLoader",
                        "url loader");
    compare_id_to_words("OnUrlLoader",
                        "on url loader");
    compare_id_to_words("kUrlLoader",
                        "url loader");
    compare_id_to_words("kOnUrlLoader",
                        "on url loader");
    compare_id_to_words("WhatIfSomeoneDoes_This",
                        "what if someone does this");
    compare_id_to_words("SOME_CONST",
                        "some const");
    compare_id_to_words("NAME_MIN_LEN",
                        "name min len");
    compare_id_to_words("OnPress",
                        "on press");

    END_TEST;
}

bool case_test(std::string case_name,
               fit::function<bool(std::string)> is_case,
               fit::function<std::string(std::string)> to_case,
               std::string original, std::string expected) {
    ASSERT_FALSE(is_case(original),
                 (original + " is " + case_name).c_str());
    std::string converted = to_case(original);
    ASSERT_STRING_EQ(converted, expected,
                     (converted + " != " + expected).c_str());
    ASSERT_TRUE(is_case(converted),
                (converted + " is not " + case_name).c_str());
    return true;
}

#define ASSERT_CASE(CASE, FROM, TO) \
    ASSERT_TRUE(case_test(#CASE, is_##CASE##_case, to_##CASE##_case, FROM, TO))

bool upper_camel_case() {
    BEGIN_TEST;

    ASSERT_CASE(upper_camel, "URLLoader", "UrlLoader");
    ASSERT_CASE(upper_camel, "is_21Jump_street", "Is21JumpStreet");
    ASSERT_CASE(upper_camel, "URLloader", "UrLloader");
    ASSERT_CASE(upper_camel, "URLLoader", "UrlLoader");
    ASSERT_CASE(upper_camel, "url_loader", "UrlLoader");
    ASSERT_CASE(upper_camel, "URL_LOADER", "UrlLoader");
    ASSERT_CASE(upper_camel, "urlLoader", "UrlLoader");
    ASSERT_CASE(upper_camel, "kUrlLoader", "UrlLoader");
    ASSERT_CASE(upper_camel, "kURLLoader", "UrlLoader");

    END_TEST;
}

bool lower_camel_case() {
    BEGIN_TEST;

    ASSERT_CASE(lower_camel, "URLLoader", "urlLoader");
    ASSERT_CASE(lower_camel, "is_21Jump_street", "is21JumpStreet");
    ASSERT_CASE(lower_camel, "URLloader", "urLloader");
    ASSERT_CASE(lower_camel, "UrlLoader", "urlLoader");
    ASSERT_CASE(lower_camel, "URLLoader", "urlLoader");
    ASSERT_CASE(lower_camel, "url_loader", "urlLoader");
    ASSERT_CASE(lower_camel, "URL_LOADER", "urlLoader");
    ASSERT_CASE(lower_camel, "kUrlLoader", "urlLoader");
    ASSERT_CASE(lower_camel, "kURLLoader", "urlLoader");

    END_TEST;
}

bool upper_snake_case() {
    BEGIN_TEST;

    ASSERT_CASE(upper_snake, "URLLoader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "is_21Jump_street", "IS_21_JUMP_STREET");
    ASSERT_CASE(upper_snake, "URLloader", "UR_LLOADER");
    ASSERT_CASE(upper_snake, "UrlLoader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "URLLoader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "url_loader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "urlLoader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "kUrlLoader", "URL_LOADER");
    ASSERT_CASE(upper_snake, "kURLLoader", "URL_LOADER");

    END_TEST;
}

bool lower_snake_case() {
    BEGIN_TEST;

    ASSERT_CASE(lower_snake, "URLLoader", "url_loader");
    ASSERT_CASE(lower_snake, "is_21Jump_street", "is_21_jump_street");
    ASSERT_CASE(lower_snake, "URLloader", "ur_lloader");
    ASSERT_CASE(lower_snake, "UrlLoader", "url_loader");
    ASSERT_CASE(lower_snake, "URLLoader", "url_loader");
    ASSERT_CASE(lower_snake, "URL_LOADER", "url_loader");
    ASSERT_CASE(lower_snake, "urlLoader", "url_loader");
    ASSERT_CASE(lower_snake, "kUrlLoader", "url_loader");
    ASSERT_CASE(lower_snake, "kURLLoader", "url_loader");

    END_TEST;
}

bool konstant_case() {
    BEGIN_TEST;

    ASSERT_CASE(konstant, "URLLoader", "kUrlLoader");
    ASSERT_CASE(konstant, "is_21Jump_street", "kIs21JumpStreet");
    ASSERT_CASE(konstant, "URLloader", "kUrLloader");
    ASSERT_CASE(konstant, "UrlLoader", "kUrlLoader");
    ASSERT_CASE(konstant, "URLLoader", "kUrlLoader");
    ASSERT_CASE(konstant, "url_loader", "kUrlLoader");
    ASSERT_CASE(konstant, "URL_LOADER", "kUrlLoader");
    ASSERT_CASE(konstant, "urlLoader", "kUrlLoader");
    ASSERT_CASE(konstant, "kURLLoader", "kUrlLoader");

    END_TEST;
}

bool lower_no_separator_case() {
    BEGIN_TEST;

    ASSERT_CASE(lower_no_separator, "URLLoader", "urlloader");
    ASSERT_CASE(lower_no_separator, "is_21Jump_street", "is21jumpstreet");
    ASSERT_CASE(lower_no_separator, "URLloader", "urlloader");
    ASSERT_CASE(lower_no_separator, "UrlLoader", "urlloader");
    ASSERT_CASE(lower_no_separator, "URLLoader", "urlloader");
    ASSERT_CASE(lower_no_separator, "url_loader", "urlloader");
    ASSERT_CASE(lower_no_separator, "URL_LOADER", "urlloader");
    ASSERT_CASE(lower_no_separator, "urlLoader", "urlloader");
    ASSERT_CASE(lower_no_separator, "kUrlLoader", "urlloader");
    ASSERT_CASE(lower_no_separator, "kURLLoader", "urlloader");

    END_TEST;
}

BEGIN_TEST_CASE(utils_tests)

RUN_TEST(id_to_words)
RUN_TEST(upper_camel_case)
RUN_TEST(lower_camel_case)
RUN_TEST(upper_snake_case)
RUN_TEST(lower_snake_case)
RUN_TEST(konstant_case)
RUN_TEST(lower_no_separator_case)

END_TEST_CASE(utils_tests)

} // namespace

} // namespace utils
} // namespace fidl
