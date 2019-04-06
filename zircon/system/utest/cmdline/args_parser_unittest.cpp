// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline/args_parser.h>
#include <unittest/unittest.h>

#define ASSERT_STRING_EQ(lhs, rhs, ...) \
    ASSERT_STR_EQ(                      \
        std::string(lhs).c_str(), std::string(rhs).c_str(), ##__VA_ARGS__)

#define EXPECT_STRING_EQ(lhs, rhs, ...) \
    EXPECT_STR_EQ(                      \
        std::string(lhs).c_str(), std::string(rhs).c_str(), ##__VA_ARGS__)

namespace cmdline {

namespace {

bool general() {
    BEGIN_TEST;

    GeneralArgsParser parser;

    bool bool_set = false;
    parser.AddGeneralSwitch("bool", 'b', "bool help",
                            [&bool_set]() { bool_set = true; });

    std::string string_option_val;
    parser.AddGeneralSwitch("str", 's', "string help",
                            [&string_option_val](const std::string& s) -> Status {
                                string_option_val = s;
                                return Status::Ok();
                            });

    parser.AddGeneralSwitch(
        "always_bad", 0, "always bad help",
        [](const std::string& s) -> Status { return Status::Error("no"); });

    // Help text.
    EXPECT_STRING_EQ(R"(always bad help

bool help

string help

)",
                     parser.GetHelp());

    // No params should always succeed.
    std::vector<std::string> args;
    const char* no_params[1] = {"program"};
    Status status = parser.ParseGeneral(1, no_params, &args);
    EXPECT_FALSE(status.has_error());
    EXPECT_TRUE(args.empty());

    // One non-option parameter.
    const char* one_non_opt[2] = {"program", "param"};
    status = parser.ParseGeneral(2, one_non_opt, &args);
    EXPECT_FALSE(status.has_error());
    ASSERT_EQ(1u, args.size());
    EXPECT_STRING_EQ("param", args[0]);

    // Long options with values. Also checks switches after first non-switch.
    args.clear();
    const char* some_params[5] = {"program", "--bool", "--str=foo", "param",
                                  "--non-switch"};
    status = parser.ParseGeneral(5, some_params, &args);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());
    EXPECT_TRUE(bool_set);
    EXPECT_STRING_EQ("foo", string_option_val);
    ASSERT_EQ(2u, args.size());
    EXPECT_STRING_EQ("param", args[0]);
    EXPECT_STRING_EQ("--non-switch", args[1]);

    // Long option with no equals.
    args.clear();
    bool_set = false;
    string_option_val.clear();
    const char* long_no_equals[5] = {"program", "--str", "foo2", "--bool",
                                     "param"};
    status = parser.ParseGeneral(5, long_no_equals, &args);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());
    EXPECT_TRUE(bool_set);
    EXPECT_STRING_EQ("foo2", string_option_val);
    ASSERT_EQ(1u, args.size());
    EXPECT_STRING_EQ("param", args[0]);

    // Short option with a space.
    args.clear();
    bool_set = false;
    string_option_val.clear();
    const char* short_with_space[5] = {"program", "-s", "foo2", "-b", "param"};
    status = parser.ParseGeneral(5, short_with_space, &args);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());
    EXPECT_TRUE(bool_set);
    EXPECT_STRING_EQ("foo2", string_option_val);
    ASSERT_EQ(1u, args.size());
    EXPECT_STRING_EQ("param", args[0]);

    // Short option with no space.
    args.clear();
    bool_set = false;
    string_option_val.clear();
    const char* short_no_space[2] = {"program", "-sfoo3"};
    status = parser.ParseGeneral(2, short_no_space, &args);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());
    EXPECT_FALSE(bool_set);
    EXPECT_STRING_EQ("foo3", string_option_val);
    EXPECT_TRUE(args.empty());

    // Single hyphen by itself is counted as a parameter and not a switch (seems
    // to match
    // most Unix tools' behavior).
    args.clear();
    bool_set = false;
    string_option_val.clear();
    const char* single_hyphen[3] = {"program", "-", "foo"};
    status = parser.ParseGeneral(3, single_hyphen, &args);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());
    ASSERT_EQ(2u, args.size());
    EXPECT_STRING_EQ("-", args[0]);
    EXPECT_STRING_EQ("foo", args[1]);

    // Short option missing param should print the help for it.
    const char* short_no_param[2] = {"program", "-s"};
    status = parser.ParseGeneral(2, short_no_param, &args);
    EXPECT_TRUE(status.has_error());
    EXPECT_STRING_EQ("-s expects an argument but none was given.\n\nstring help",
                     status.error_message().c_str());

    // Long option missing param.
    const char* long_no_param[2] = {"program", "--str"};
    status = parser.ParseGeneral(2, long_no_param, &args);
    EXPECT_TRUE(status.has_error());
    EXPECT_STRING_EQ("--str expects an argument but none was given.\n\nstring help",
                     status.error_message().c_str());

    // Using -- to end the options.
    args.clear();
    bool_set = false;
    string_option_val.clear();
    const char* dash_dash[4] = {"program", "--", "--str", "--bool"};
    status = parser.ParseGeneral(4, dash_dash, &args);
    EXPECT_FALSE(status.has_error());
    ASSERT_EQ(2u, args.size());
    EXPECT_STRING_EQ("--str", args[0]);
    EXPECT_TRUE(string_option_val.empty());
    EXPECT_STRING_EQ("--bool", args[1]);
    EXPECT_FALSE(bool_set);

    END_TEST;
}

bool opt_struct() {
    BEGIN_TEST;

    struct MyOptions {
        bool present = false;
        bool not_present = false;

        std::optional<std::string> present_str;
        std::optional<std::string> not_present_str;
    };
    ArgsParser<MyOptions> parser;
    parser.AddSwitch("present", 'p', "p help", &MyOptions::present);
    parser.AddSwitch("not_present", 'n', "np help", &MyOptions::not_present);
    parser.AddSwitch("ps", 'q', "ps help", &MyOptions::present_str);
    parser.AddSwitch("nps", 'o', "nps help", &MyOptions::not_present_str);

    const char* argv[4] = {"program", "--present", "--ps=foo", "bar"};

    MyOptions options;
    std::vector<std::string> params;
    Status status = parser.Parse(4, argv, &options, &params);
    EXPECT_FALSE(status.has_error(), status.error_message().c_str());

    EXPECT_TRUE(options.present);
    EXPECT_FALSE(options.not_present);

    EXPECT_TRUE(options.present_str);
    EXPECT_STRING_EQ("foo", *options.present_str);
    EXPECT_FALSE(options.not_present_str);

    ASSERT_EQ(1u, params.size());
    EXPECT_STRING_EQ("bar", params[0]);

    END_TEST;
}

BEGIN_TEST_CASE(args_parser)
RUN_TEST(general)
RUN_TEST(opt_struct)
END_TEST_CASE(args_parser)

} // namespace

} // namespace cmdline
