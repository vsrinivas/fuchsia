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

bool case_test(bool valid_conversion,
               std::string case_name,
               fit::function<bool(std::string)> is_case,
               fit::function<std::string(std::string)> to_case,
               std::string original, std::string expected) {
    EXPECT_FALSE(is_case(original),
                 (original + " is " + case_name).c_str());
    std::string converted = to_case(original);
    EXPECT_STRING_EQ(converted, expected,
                     ("from '" + original +
                      "' to '" + converted +
                      "' != '" + expected + "'")
                         .c_str());
    if (valid_conversion) {
        EXPECT_TRUE(is_case(expected),
                    ("from '" + original +
                     "' expected '" + expected +
                     "' is not " + case_name)
                        .c_str());
        EXPECT_TRUE(is_case(converted),
                    ("from '" + original +
                     "' to '" + converted +
                     "' is not " + case_name)
                        .c_str());
    } else {
        EXPECT_FALSE(is_case(converted),
                     ("from '" + original +
                      "' to '" + converted +
                      "' was not expected to match " + case_name + ", but did!")
                         .c_str());
    }
    return true;
}

#define ASSERT_CASE(CASE, FROM, TO)                         \
    EXPECT_TRUE(case_test(/*valid_conversion=*/true, #CASE, \
                          is_##CASE##_case, to_##CASE##_case, FROM, TO))

#define ASSERT_BAD_CASE(CASE, FROM, TO)                      \
    EXPECT_TRUE(case_test(/*valid_conversion=*/false, #CASE, \
                          is_##CASE##_case, to_##CASE##_case, FROM, TO))

bool upper_camel_case() {
    BEGIN_TEST;

    ASSERT_CASE(upper_camel, "x", "X");
    ASSERT_CASE(upper_camel, "xy", "Xy");
    ASSERT_BAD_CASE(upper_camel, "x_y", "XY");
    ASSERT_CASE(upper_camel, "xyz_123", "Xyz123");
    ASSERT_CASE(upper_camel, "xy_z_123", "XyZ123");
    ASSERT_CASE(upper_camel, "xy_z123", "XyZ123");
    ASSERT_CASE(upper_camel, "days_in_a_week", "DaysInAWeek");
    ASSERT_CASE(upper_camel, "android8_0_0", "Android8_0_0");
    ASSERT_CASE(upper_camel, "android_8_0_0", "Android8_0_0");
    ASSERT_CASE(upper_camel, "x_marks_the_spot", "XMarksTheSpot");
    ASSERT_CASE(upper_camel, "RealID", "RealId");
    ASSERT_CASE(upper_camel, "real_id", "RealId");
    ASSERT_BAD_CASE(upper_camel, "real_i_d", "RealID");
    ASSERT_CASE(upper_camel, "real3d", "Real3d");
    ASSERT_CASE(upper_camel, "real3_d", "Real3D");
    ASSERT_CASE(upper_camel, "real_3d", "Real3d");
    ASSERT_CASE(upper_camel, "real_3_d", "Real3D");
    ASSERT_CASE(upper_camel, "sample_x_union", "SampleXUnion");
    ASSERT_CASE(upper_camel, "sample_xunion", "SampleXunion");
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

    ASSERT_CASE(lower_camel, "X", "x");
    ASSERT_CASE(lower_camel, "XY", "xy");
    ASSERT_CASE(lower_camel, "X_Y", "xY");
    ASSERT_CASE(lower_camel, "XYZ_123", "xyz123");
    ASSERT_CASE(lower_camel, "XY_Z_123", "xyZ123");
    ASSERT_CASE(lower_camel, "XY_Z123", "xyZ123");
    ASSERT_CASE(lower_camel, "DAYS_IN_A_WEEK", "daysInAWeek");
    ASSERT_CASE(lower_camel, "ANDROID8_0_0", "android8_0_0");
    ASSERT_CASE(lower_camel, "ANDROID_8_0_0", "android8_0_0");
    ASSERT_CASE(lower_camel, "X_MARKS_THE_SPOT", "xMarksTheSpot");
    ASSERT_CASE(lower_camel, "realID", "realId");
    ASSERT_CASE(lower_camel, "REAL_ID", "realId");
    ASSERT_BAD_CASE(lower_camel, "REAL_I_D", "realID");
    ASSERT_CASE(lower_camel, "REAL3D", "real3D");
    ASSERT_CASE(lower_camel, "REAL3_D", "real3D");
    ASSERT_CASE(lower_camel, "REAL_3D", "real3D");
    ASSERT_CASE(lower_camel, "REAL_3_D", "real3D");
    ASSERT_CASE(lower_camel, "SAMPLE_X_UNION", "sampleXUnion");
    ASSERT_CASE(lower_camel, "SAMPLE_XUNION", "sampleXunion");
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

    ASSERT_CASE(upper_snake, "x", "X");
    ASSERT_CASE(upper_snake, "xy", "XY");
    ASSERT_CASE(upper_snake, "xY", "X_Y");
    ASSERT_CASE(upper_snake, "xyz123", "XYZ123");
    ASSERT_CASE(upper_snake, "xyz_123", "XYZ_123");
    ASSERT_CASE(upper_snake, "xyZ123", "XY_Z123");
    ASSERT_CASE(upper_snake, "daysInAWeek", "DAYS_IN_A_WEEK");
    ASSERT_CASE(upper_snake, "android8_0_0", "ANDROID8_0_0");
    ASSERT_CASE(upper_snake, "android_8_0_0", "ANDROID_8_0_0");
    ASSERT_CASE(upper_snake, "xMarksTheSpot", "X_MARKS_THE_SPOT");
    ASSERT_CASE(upper_snake, "realId", "REAL_ID");
    ASSERT_CASE(upper_snake, "realID", "REAL_ID");
    ASSERT_CASE(upper_snake, "real3d", "REAL3D");
    ASSERT_CASE(upper_snake, "real3D", "REAL3_D");
    ASSERT_CASE(upper_snake, "real_3d", "REAL_3D");
    ASSERT_CASE(upper_snake, "real_3D", "REAL_3_D");
    ASSERT_CASE(upper_snake, "sampleXUnion", "SAMPLE_X_UNION");
    ASSERT_CASE(upper_snake, "sampleXunion", "SAMPLE_XUNION");
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

    ASSERT_CASE(lower_snake, "X", "x");
    ASSERT_CASE(lower_snake, "Xy", "xy");
    ASSERT_CASE(lower_snake, "XY", "xy");
    ASSERT_CASE(lower_snake, "Xyz123", "xyz123");
    ASSERT_CASE(lower_snake, "Xyz_123", "xyz_123");
    ASSERT_CASE(lower_snake, "XyZ123", "xy_z123");
    ASSERT_CASE(lower_snake, "DaysInAWeek", "days_in_a_week");
    ASSERT_CASE(lower_snake, "Android8_0_0", "android8_0_0");
    ASSERT_CASE(lower_snake, "Android_8_0_0", "android_8_0_0");
    ASSERT_CASE(lower_snake, "XMarksTheSpot", "x_marks_the_spot");
    ASSERT_CASE(lower_snake, "RealId", "real_id");
    ASSERT_CASE(lower_snake, "RealID", "real_id");
    ASSERT_CASE(lower_snake, "Real3d", "real3d");
    ASSERT_CASE(lower_snake, "Real3D", "real3_d");
    ASSERT_CASE(lower_snake, "Real_3d", "real_3d");
    ASSERT_CASE(lower_snake, "Real_3D", "real_3_d");
    ASSERT_CASE(lower_snake, "SampleXUnion", "sample_x_union");
    ASSERT_CASE(lower_snake, "SampleXunion", "sample_xunion");
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

bool whitespace_and_comments() {
    BEGIN_TEST;

    ASSERT_TRUE(IsWhitespace(' '));
    ASSERT_TRUE(IsWhitespace('\t'));
    ASSERT_TRUE(IsWhitespace('\v'));
    ASSERT_TRUE(IsWhitespace('\f'));
    ASSERT_TRUE(IsWhitespace('\r'));
    ASSERT_TRUE(IsWhitespace('\n'));
    ASSERT_FALSE(IsWhitespace('\0'));
    ASSERT_FALSE(IsWhitespace('_'));
    ASSERT_FALSE(IsWhitespace('-'));
    ASSERT_FALSE(IsWhitespace('A'));
    ASSERT_FALSE(IsWhitespace('Z'));
    ASSERT_FALSE(IsWhitespace('a'));
    ASSERT_FALSE(IsWhitespace('z'));
    ASSERT_FALSE(IsWhitespace('0'));
    ASSERT_FALSE(IsWhitespace('9'));
    ASSERT_FALSE(IsWhitespace('!'));

    ASSERT_TRUE(IsWhitespaceNoNewline(' '));
    ASSERT_TRUE(IsWhitespaceNoNewline('\t'));
    ASSERT_TRUE(IsWhitespaceNoNewline('\v'));
    ASSERT_TRUE(IsWhitespaceNoNewline('\f'));
    ASSERT_TRUE(IsWhitespaceNoNewline('\r'));
    ASSERT_FALSE(IsWhitespaceNoNewline('\n'));
    ASSERT_FALSE(IsWhitespaceNoNewline('\0'));
    ASSERT_FALSE(IsWhitespaceNoNewline('_'));
    ASSERT_FALSE(IsWhitespaceNoNewline('-'));
    ASSERT_FALSE(IsWhitespaceNoNewline('A'));
    ASSERT_FALSE(IsWhitespaceNoNewline('Z'));
    ASSERT_FALSE(IsWhitespaceNoNewline('a'));
    ASSERT_FALSE(IsWhitespaceNoNewline('z'));
    ASSERT_FALSE(IsWhitespaceNoNewline('0'));
    ASSERT_FALSE(IsWhitespaceNoNewline('9'));
    ASSERT_FALSE(IsWhitespaceNoNewline('!'));

    ASSERT_TRUE(IsBlank(""));
    ASSERT_TRUE(IsBlank(" "));
    ASSERT_TRUE(IsBlank("\t"));
    ASSERT_TRUE(IsBlank("\n"));
    ASSERT_TRUE(IsBlank("\n\n\n"));
    ASSERT_TRUE(IsBlank("  \n  \n  \n"));
    ASSERT_TRUE(IsBlank(" \t\v\f\r\n"));
    ASSERT_TRUE(IsBlank("     "));
    ASSERT_TRUE(IsBlank(" \t \t "));
    ASSERT_TRUE(IsBlank("\t \t \t"));
    ASSERT_FALSE(IsBlank("multi\nline"));
    ASSERT_FALSE(IsBlank("\nmore\nmulti\nline\n"));
    ASSERT_FALSE(IsBlank("\t\t."));
    ASSERT_FALSE(IsBlank("    ."));
    ASSERT_FALSE(IsBlank(".    "));
    ASSERT_FALSE(IsBlank("// Comment "));
    ASSERT_FALSE(IsBlank("/// Doc Comment "));

    ASSERT_TRUE(LineFromOffsetIsBlank("four", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four\n", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    ", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four  \t \t  ", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \t\n", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \n\t", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \nmore lines", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \nmore lines\n", 4));
    ASSERT_TRUE(LineFromOffsetIsBlank("four    \t\n\t", 4));
    ASSERT_FALSE(LineFromOffsetIsBlank("four.", 4));
    ASSERT_FALSE(LineFromOffsetIsBlank("four.\n", 4));
    ASSERT_FALSE(LineFromOffsetIsBlank("fournot blank    \n", 4));
    ASSERT_FALSE(LineFromOffsetIsBlank("four    more chars", 4));
    ASSERT_FALSE(LineFromOffsetIsBlank("four    more chars\n", 4));

    ASSERT_TRUE(FirstLineIsBlank(""));
    ASSERT_TRUE(FirstLineIsBlank(""));
    ASSERT_TRUE(FirstLineIsBlank("\n"));
    ASSERT_TRUE(FirstLineIsBlank("    "));
    ASSERT_TRUE(FirstLineIsBlank("    \n"));
    ASSERT_TRUE(FirstLineIsBlank("  \t \t  "));
    ASSERT_TRUE(FirstLineIsBlank("    \n"));
    ASSERT_TRUE(FirstLineIsBlank("    \t\n"));
    ASSERT_TRUE(FirstLineIsBlank("    \nmore lines"));
    ASSERT_TRUE(FirstLineIsBlank("    \nmore lines\n"));
    ASSERT_TRUE(FirstLineIsBlank("    \n\t"));
    ASSERT_TRUE(FirstLineIsBlank("    \t\n\t"));
    ASSERT_FALSE(FirstLineIsBlank("."));
    ASSERT_FALSE(FirstLineIsBlank(".\n"));
    ASSERT_FALSE(FirstLineIsBlank("not blank    \n"));
    ASSERT_FALSE(FirstLineIsBlank("    more chars"));
    ASSERT_FALSE(FirstLineIsBlank("    more chars\n"));

    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//  \t\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//not blank    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//  not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//not blank    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//  not blank\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//not blank\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    \n\t", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    \t\n\t", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    \nmore lines", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four//    \nmore lines\n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four.//", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four    .//\n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("fourmore//    ", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four    more\n//    \n", 4));
    //// Greater than 3 slashes are still interpreted as a regular comment
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////  \t\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////not blank    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////  not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////not blank    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////  not blank\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four////not blank\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////  \t\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////not blank    ", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////  not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////not blank", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////not blank    \n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////  not blank\n", 4));
    ASSERT_TRUE(LineFromOffsetIsRegularComment("four/////not blank\n", 4));
    /// FIDL Doc Comments start with 3 slashes, like this one
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///\n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///    ", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///    \n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///  \t\n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///not blank    ", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///  not blank", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///not blank", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///not blank    \n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///  not blank\n", 4));
    ASSERT_FALSE(LineFromOffsetIsRegularComment("four///not blank\n", 4));

    ASSERT_TRUE(FirstLineIsRegularComment("//"));
    ASSERT_TRUE(FirstLineIsRegularComment("//\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//    "));
    ASSERT_TRUE(FirstLineIsRegularComment("//    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//  \t\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//not blank    "));
    ASSERT_TRUE(FirstLineIsRegularComment("//  not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("//not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("//not blank    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//  not blank\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//not blank\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("//    \n\t"));
    ASSERT_TRUE(FirstLineIsRegularComment("//    \t\n\t"));
    ASSERT_TRUE(FirstLineIsRegularComment("//    \nmore lines"));
    ASSERT_TRUE(FirstLineIsRegularComment("//    \nmore lines\n"));
    ASSERT_FALSE(FirstLineIsRegularComment(".//"));
    ASSERT_FALSE(FirstLineIsRegularComment("    .//\n"));
    ASSERT_FALSE(FirstLineIsRegularComment("more//    "));
    ASSERT_FALSE(FirstLineIsRegularComment("    more\n//    \n"));
    //// Greater than 3 slashes are still interpreted as a regular comment
    ASSERT_TRUE(FirstLineIsRegularComment("////"));
    ASSERT_TRUE(FirstLineIsRegularComment("////\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("////    "));
    ASSERT_TRUE(FirstLineIsRegularComment("////    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("////  \t\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("////not blank    "));
    ASSERT_TRUE(FirstLineIsRegularComment("////  not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("////not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("////not blank    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("////  not blank\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("////not blank\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////    "));
    ASSERT_TRUE(FirstLineIsRegularComment("/////    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////  \t\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////not blank    "));
    ASSERT_TRUE(FirstLineIsRegularComment("/////  not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////not blank"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////not blank    \n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////  not blank\n"));
    ASSERT_TRUE(FirstLineIsRegularComment("/////not blank\n"));
    /// FIDL Doc Comments start with 3 slashes, like this one
    ASSERT_FALSE(FirstLineIsRegularComment("///"));
    ASSERT_FALSE(FirstLineIsRegularComment("///\n"));
    ASSERT_FALSE(FirstLineIsRegularComment("///    "));
    ASSERT_FALSE(FirstLineIsRegularComment("///    \n"));
    ASSERT_FALSE(FirstLineIsRegularComment("///  \t\n"));
    ASSERT_FALSE(FirstLineIsRegularComment("///not blank    "));
    ASSERT_FALSE(FirstLineIsRegularComment("///  not blank"));
    ASSERT_FALSE(FirstLineIsRegularComment("///not blank"));
    ASSERT_FALSE(FirstLineIsRegularComment("///not blank    \n"));
    ASSERT_FALSE(FirstLineIsRegularComment("///  not blank\n"));
    ASSERT_FALSE(FirstLineIsRegularComment("///not blank\n"));

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
RUN_TEST(whitespace_and_comments)

END_TEST_CASE(utils_tests)

} // namespace

} // namespace utils
} // namespace fidl
