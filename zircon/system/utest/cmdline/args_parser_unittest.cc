// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>

#include <zxtest/zxtest.h>

#define ASSERT_STRING_EQ(lhs, rhs, ...) \
  ASSERT_STR_EQ(std::string(lhs).c_str(), std::string(rhs).c_str(), ##__VA_ARGS__)

#define EXPECT_STRING_EQ(lhs, rhs, ...) \
  EXPECT_STR_EQ(std::string(lhs).c_str(), std::string(rhs).c_str(), ##__VA_ARGS__)

namespace cmdline {

namespace {

TEST(ArgsParser, General) {
  GeneralArgsParser parser;

  bool bool_set = false;
  parser.AddGeneralSwitch("bool", 'b', "bool help", [&bool_set]() { bool_set = true; });

  bool on_off_switch = true;
  parser.AddGeneralSwitch(
      "on-off-switch", 'o', "[no]on-off-switch help", [&on_off_switch]() { on_off_switch = true; },
      [&on_off_switch]() { on_off_switch = false; });

  std::string string_option_val;
  parser.AddGeneralSwitch("str", 's', "string help",
                          [&string_option_val](const std::string& s) -> Status {
                            string_option_val = s;
                            return Status::Ok();
                          });

  parser.AddGeneralSwitch("always_bad", 0, "always bad help",
                          [](const std::string& s) -> Status { return Status::Error("no"); });

  // Help text.
  EXPECT_STRING_EQ(R"([no]on-off-switch help

always bad help

bool help

string help

)",
                   parser.GetHelp());

  // No params should always succeed.
  std::vector<std::string> args;
  const char* no_params[1] = {"program"};
  Status status = parser.ParseGeneral(1, no_params, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_TRUE(args.empty());

  // One non-option parameter.
  const char* one_non_opt[2] = {"program", "param"};
  status = parser.ParseGeneral(2, one_non_opt, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  ASSERT_EQ(1u, args.size());
  EXPECT_STRING_EQ("param", args[0]);
  EXPECT_FALSE(bool_set);
  EXPECT_TRUE(on_off_switch);

  // Long options with values. Also checks switches after first non-switch.
  args.clear();
  const char* some_params[6] = {"program",           "--bool", "--str=foo",
                                "--noon-off-switch", "param",  "--non-switch"};
  status = parser.ParseGeneral(6, some_params, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_TRUE(bool_set);
  EXPECT_STRING_EQ("foo", string_option_val);
  EXPECT_FALSE(on_off_switch);
  ASSERT_EQ(2u, args.size());
  EXPECT_STRING_EQ("param", args[0]);
  EXPECT_STRING_EQ("--non-switch", args[1]);

  // Long option with no equals.
  args.clear();
  bool_set = false;
  string_option_val.clear();
  const char* long_no_equals[6] = {"program", "--str",           "foo2",
                                   "--bool",  "--on-off-switch", "param"};
  status = parser.ParseGeneral(6, long_no_equals, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_TRUE(bool_set);
  EXPECT_STRING_EQ("foo2", string_option_val);
  EXPECT_TRUE(on_off_switch);
  ASSERT_EQ(1u, args.size());
  EXPECT_STRING_EQ("param", args[0]);

  // Short option with a space.
  args.clear();
  bool_set = false;
  string_option_val.clear();
  const char* short_with_space[6] = {"program", "-s", "foo3", "-b", "-o", "param"};
  status = parser.ParseGeneral(6, short_with_space, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_STRING_EQ("foo3", string_option_val);
  EXPECT_TRUE(bool_set);
  EXPECT_TRUE(on_off_switch);
  ASSERT_EQ(1u, args.size());
  EXPECT_STRING_EQ("param", args[0]);

  // Short option with no space.
  args.clear();
  bool_set = false;
  string_option_val.clear();
  const char* short_no_space[3] = {"program", "-sfoo4", "-o"};
  status = parser.ParseGeneral(3, short_no_space, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_FALSE(bool_set);
  EXPECT_TRUE(on_off_switch);
  EXPECT_STRING_EQ("foo4", string_option_val);
  EXPECT_TRUE(args.empty());

  // Single hyphen by itself is counted as a parameter and not a switch (seems
  // to match
  // most Unix tools' behavior).
  args.clear();
  bool_set = false;
  string_option_val.clear();
  const char* single_hyphen[3] = {"program", "-", "foo"};
  status = parser.ParseGeneral(3, single_hyphen, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  ASSERT_EQ(2u, args.size());
  EXPECT_STRING_EQ("-", args[0]);
  EXPECT_STRING_EQ("foo", args[1]);

  // Short option missing param should print the help for it.
  const char* short_no_param[2] = {"program", "-s"};
  status = parser.ParseGeneral(2, short_no_param, &args);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("-s expects an argument but none was given.\n\nstring help",
                   status.error_message());

  // Long option missing param.
  const char* long_no_param[2] = {"program", "--str"};
  status = parser.ParseGeneral(2, long_no_param, &args);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("--str expects an argument but none was given.\n\nstring help",
                   status.error_message());

  // Long bool without support for off switch
  const char* long_no_off_switch[2] = {"program", "--nobool"};
  status = parser.ParseGeneral(2, long_no_off_switch, &args);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("--bool can only be turned on, not off.\n\nbool help", status.error_message());

  // Invalid option
  const char* invalid_option[2] = {"program", "--notvalid"};
  status = parser.ParseGeneral(2, invalid_option, &args);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("--notvalid is not a valid option. Try --help", status.error_message());

  // Using -- to end the options.
  args.clear();
  bool_set = false;
  string_option_val.clear();
  const char* dash_dash[4] = {"program", "--", "--str", "--bool"};
  status = parser.ParseGeneral(4, dash_dash, &args);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());
  ASSERT_EQ(2u, args.size());
  EXPECT_STRING_EQ("--str", args[0]);
  EXPECT_TRUE(string_option_val.empty());
  EXPECT_STRING_EQ("--bool", args[1]);
  EXPECT_FALSE(bool_set);
}

TEST(ArgsParser, OptStruct) {
  struct MyOptions {
    bool present = false;
    bool not_present = false;

    std::optional<std::string> present_str;
    std::optional<std::string> not_present_str;

    bool on_by_default = true;
    int int_val = 2;
    size_t size_t_val = 25;
    double double_val = 2.718;
    char char_val = 'A';
    std::string not_optional_str;  // but empty if not present
    Optional<bool> optional_bool;
    std::string validated_format = "text";
  };
  ArgsParser<MyOptions> parser;
  parser.AddSwitch("present", 'p', "p help", &MyOptions::present);
  parser.AddSwitch("not_present", 'n', "np help", &MyOptions::not_present);
  parser.AddSwitch("ps", 'q', "ps help", &MyOptions::present_str);
  parser.AddSwitch("nps", 'o', "nps help", &MyOptions::not_present_str);
  parser.AddSwitch("validated-format", 0, "validated-format help", &MyOptions::validated_format,
                   [](const std::string& format) -> Status {
                     if (format == "text" || format == "json") {
                       return Status::Ok();
                     }
                     return Status::Error("Invalid value for --format: " + format);
                   });
  parser.AddSwitch("on_by_default", 0, "on_by_default help", &MyOptions::on_by_default);
  parser.AddSwitch("int", 0, "int help", &MyOptions::int_val);
  parser.AddSwitch("size_t", 0, "size_t help", &MyOptions::size_t_val);
  parser.AddSwitch("double", 0, "double help", &MyOptions::double_val);
  parser.AddSwitch("char", 0, "char help", &MyOptions::char_val);
  parser.AddSwitch("not-optional-str", 0, "not-optional-str help", &MyOptions::not_optional_str);
  parser.AddSwitch("optional-bool", 0, "optional-bool help", &MyOptions::optional_bool);

  const char* bool_and_optional_str[4] = {"program", "--present", "--ps=foo", "bar"};

  MyOptions options;
  std::vector<std::string> params;
  Status status = parser.Parse(4, bool_and_optional_str, &options, &params);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());

  EXPECT_TRUE(options.present);
  EXPECT_FALSE(options.not_present);

  EXPECT_TRUE(options.present_str);
  EXPECT_STRING_EQ("foo", *options.present_str);
  EXPECT_FALSE(options.not_present_str);

  EXPECT_TRUE(options.on_by_default);
  EXPECT_EQ(2, options.int_val);
  EXPECT_EQ(25u, options.size_t_val);
  EXPECT_EQ(2.718, options.double_val);
  EXPECT_STRING_EQ("", options.not_optional_str);
  EXPECT_FALSE(options.optional_bool.has_value());
  EXPECT_TRUE(options.optional_bool.value_or(true));
  EXPECT_FALSE(options.optional_bool.value_or(false));
  EXPECT_STRING_EQ("text", options.validated_format);

  ASSERT_EQ(1u, params.size());
  EXPECT_STRING_EQ("bar", params[0]);

  const char* off_sizet_string_optionalbool_validate[7] = {
      "program",         "--noon_by_default",       "--size_t=50", "--not-optional-str=hasvalue",
      "--optional-bool", "--validated-format=json", "bar"};

  params.clear();
  status = parser.Parse(7, off_sizet_string_optionalbool_validate, &options, &params);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());

  EXPECT_FALSE(options.on_by_default);
  EXPECT_EQ(50u, options.size_t_val);
  EXPECT_STRING_EQ("hasvalue", options.not_optional_str);
  EXPECT_TRUE(options.optional_bool.has_value());
  EXPECT_TRUE(*options.optional_bool);
  EXPECT_STRING_EQ("json", options.validated_format);

  ASSERT_EQ(1u, params.size());
  EXPECT_STRING_EQ("bar", params[0]);

  const char* optionalbool_to_false[3] = {"program", "--nooptional-bool", "bar"};

  params.clear();
  status = parser.Parse(3, optionalbool_to_false, &options, &params);
  EXPECT_FALSE(status.has_error(), "%s", status.error_message().c_str());

  EXPECT_TRUE(options.optional_bool.has_value());
  EXPECT_FALSE(*options.optional_bool);

  ASSERT_EQ(1u, params.size());
  EXPECT_STRING_EQ("bar", params[0]);

  const char* invalid_format[3] = {"program", "--validated-format=xml", "bar"};

  params.clear();
  status = parser.Parse(3, invalid_format, &options, &params);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ(status.error_message(), "Invalid value for --format: xml");

  // Invalid value
  const char* invalid_value_blank[3] = {"program", "--double", ""};
  params.clear();
  status = parser.Parse(3, invalid_value_blank, &options, &params);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("'' is invalid for --double", status.error_message());

  // Invalid value
  const char* invalid_value_trailing_decimal[2] = {"program", "--int=3.14"};
  params.clear();
  status = parser.Parse(2, invalid_value_trailing_decimal, &options, &params);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("Invalid trailing characters '.14' for --int", status.error_message());

  // Invalid value
  const char* invalid_value_trailing_word[2] = {"program", "--int=2718 foo"};
  params.clear();
  status = parser.Parse(2, invalid_value_trailing_word, &options, &params);
  EXPECT_TRUE(status.has_error());
  EXPECT_STRING_EQ("Invalid trailing characters 'foo' for --int", status.error_message());

  // Invalid value
  const char* invalid_value_trailing_chars[2] = {"program", "--char=hello"};
  params.clear();
  status = parser.Parse(2, invalid_value_trailing_chars, &options, &params);
  EXPECT_TRUE(status.has_error(), "%s", status.error_message().c_str());
  EXPECT_STRING_EQ("Invalid trailing characters 'ello' for --char", status.error_message());
}

TEST(ArgsParser, OptionalTypes) {
  struct MyOptions {
    Optional<std::string> present_string;
    Optional<std::string> missing_string;

    Optional<int> present_int;
    Optional<int> missing_int;

    Optional<double> present_double;
    Optional<double> missing_double;

    Optional<char> present_char;
    Optional<char> missing_char;
  };

  ArgsParser<MyOptions> parser;
  parser.AddSwitch("present_string", 0, "", &MyOptions::present_string);
  parser.AddSwitch("missing_string", 0, "", &MyOptions::missing_string);
  parser.AddSwitch("present_int", 0, "", &MyOptions::present_int);
  parser.AddSwitch("missing_int", 0, "", &MyOptions::missing_int);
  parser.AddSwitch("present_double", 0, "", &MyOptions::present_double);
  parser.AddSwitch("missing_double", 0, "", &MyOptions::missing_double);
  parser.AddSwitch("present_char", 0, "", &MyOptions::present_char);
  parser.AddSwitch("missing_char", 0, "", &MyOptions::missing_char);

  const char* args[] = {"program", "--present_string=foo", "--present_int=3",
                        "--present_double=1.5", "--present_char=x"};

  MyOptions options;
  std::vector<std::string> params;
  Status status = parser.Parse(5, args, &options, &params);
  ASSERT_TRUE(status.ok());

  EXPECT_FALSE(options.missing_string.has_value());
  EXPECT_FALSE(options.missing_int.has_value());
  EXPECT_FALSE(options.missing_double.has_value());
  EXPECT_FALSE(options.missing_char.has_value());

  EXPECT_STRING_EQ(options.present_string.value(), "foo");
  EXPECT_EQ(options.present_int.value(), 3);
  EXPECT_EQ(options.present_double.value(), 1.5);
  EXPECT_EQ(options.present_char.value(), 'x');
}

TEST(ArgsParser, OptionalTypeWithDefault) {
  struct MyOptions {
    std::optional<std::string> present_string = "x";
    std::optional<std::string> missing_string = "y";

    Optional<int> present_int = 1;
    Optional<int> missing_int = 2;
  };

  ArgsParser<MyOptions> parser;
  parser.AddSwitch("present_string", 0, "", &MyOptions::present_string);
  parser.AddSwitch("missing_string", 0, "", &MyOptions::missing_string);
  parser.AddSwitch("present_int", 0, "", &MyOptions::present_int);
  parser.AddSwitch("missing_int", 0, "", &MyOptions::missing_int);

  const char* args[] = {"program", "--present_string=foo", "--present_int=3"};

  MyOptions options;
  std::vector<std::string> params;
  Status status = parser.Parse(3, args, &options, &params);
  ASSERT_TRUE(status.ok());

  EXPECT_STRING_EQ(options.present_string.value(), "foo");
  EXPECT_EQ(options.present_int.value(), 3);

  EXPECT_STRING_EQ(options.missing_string.value(), "y");
  EXPECT_EQ(options.missing_int.value(), 2);
}

TEST(ArgsParser, VectorTypes) {
  struct MyOptions {
    std::vector<std::string> string_vector;

    std::vector<int> int_vector;

    std::vector<double> double_vector;

    std::vector<char> char_vector;
  };

  auto int_validator = [](const std::string& input) -> Status {
    // Check that the string is only digits.
    if (std::find_if(input.begin(), input.end(),
                     [](unsigned char c) { return !std::isdigit(c); }) != input.end()) {
      return Status::Error("Expected only digits, found: " + input);
    }
    return Status::Ok();
  };

  ArgsParser<MyOptions> parser;
  parser.AddSwitch("string_vector", 0, "", &MyOptions::string_vector, nullptr, ',');
  parser.AddSwitch("int_vector", 0, "", &MyOptions::int_vector, int_validator);
  parser.AddSwitch("double_vector", 'd', "", &MyOptions::double_vector);
  parser.AddSwitch("char_vector", 'c', "", &MyOptions::char_vector);

  const char* args[] = {
      "program", "--string_vector=foo,bar,baz", "--int_vector=3", "--int_vector=7",
      "-d", "1.5", "-d", "2.7", "--char_vector=x", "-c", "y", "--char_vector=z"
  };

  MyOptions options;
  std::vector<std::string> params;
  Status status = parser.Parse(12, args, &options, &params);
  ASSERT_FALSE(status.has_error(), "%s", status.error_message().c_str());

  EXPECT_EQ(options.string_vector, std::vector<std::string>({"foo", "bar", "baz"}));
  EXPECT_EQ(options.int_vector, std::vector<int>({3, 7}));
  EXPECT_EQ(options.double_vector, std::vector<double>({1.5, 2.7}));
  EXPECT_EQ(options.char_vector, std::vector<char>({'x', 'y', 'z'}));

  // Invalid value.
  const char* bad_args[] = {"program", "--int_vector=2.0"};
  status = parser.Parse(2, bad_args, &options, &params);
  EXPECT_TRUE(status.has_error());
  EXPECT_STR_EQ("Expected only digits, found: 2.0", status.error_message());
}

}  // namespace

}  // namespace cmdline
