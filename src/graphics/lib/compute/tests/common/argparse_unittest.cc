// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "argparse.h"

#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

// Class used to collect the arg_parse help message into a std::string
// Usage is the following:
//   HelpOutput output;
//   HELP_OUTPUT_GET(output, "progname", "description", MY_OPTIONS);
//   EXPECT_STREQ(".....", output.text());
//
class TestHelpOutput {
 public:
  TestHelpOutput() = default;

  const std::string &
  text() const
  {
    return text_;
  };

  // |progname| is program's command-line name.
  // |description| is short program description.
  // |option_layout| is an array generated with ARGPARSE_LIST_TO_LAYOUT_ARRAY_LITERAL()
  // Returns a reference to an std::string containing the help message.
  void
  getArgParseHelp(const char *                                progname,
                  const char *                                description,
                  const struct argparse_option_layout * const layouts)
  {
    text_.clear();
    argparse_print_help_internal(progname, description, layouts, &TestHelpOutput::printFunc, this);
  }

#define TEST_HELP_OUTPUT_GET(output, progname, description, options_list)                          \
  (output).getArgParseHelp(progname,                                                               \
                           description,                                                            \
                           ARGPARSE_LIST_TO_LAYOUT_ARRAY_LITERAL(options_list))

 private:
  // A callback that can be used as an argparse_print_func.
  // |opaque| should be a this pointer.
  static void
  printFunc(void * opaque, const char * fmt, ...)
  {
    char    buff[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);
    reinterpret_cast<TestHelpOutput *>(opaque)->text_ += buff;
  }

  std::string text_;
};

TEST(argparse, FlagOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_FLAG(param, my_flag, 'f', "flag", "My flag")

  // Short format.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.my_flag);

    const char * argv[] = { "program", "-f", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.my_flag);
    EXPECT_FALSE(options.help_needed);
  }

  // Long format
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.my_flag);

    const char * argv[] = { "program", "--flag", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.my_flag);
    EXPECT_FALSE(options.help_needed);
  }

  // None
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.my_flag);

    const char * argv[] = { "program", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_FALSE(options.my_flag);
    EXPECT_FALSE(options.help_needed);
  }

  // Multiple
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.my_flag);

    const char * argv[] = { "program", "-f", "--flag", "argument", "--flag" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.my_flag);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, CounterOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param)                                                                          \
  ARGPARSE_OPTION_COUNTER(param, verbosity, 'v', "verbose", "Increment verbosity")

  // Short format.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(0, options.verbosity);

    const char * argv[] = { "program", "-v", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_EQ(1, options.verbosity);
    EXPECT_FALSE(options.help_needed);
  }

  // Long format
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(0, options.verbosity);

    const char * argv[] = { "program", "--verbose", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_EQ(1, options.verbosity);
    EXPECT_FALSE(options.help_needed);
  }

  // None
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(0, options.verbosity);

    const char * argv[] = { "program", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_EQ(0, options.verbosity);
    EXPECT_FALSE(options.help_needed);
  }

  // Multiple
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(0, options.verbosity);

    int          argc   = 5;
    const char * argv[] = { "program", "-v", "--verbose", "argument", "--verbose" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_EQ(3, options.verbosity);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, StringOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_STRING(param, output, 'o', "output", "Output path")

  // Short format.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 4;
    const char * argv[] = { "program", "-o", "output_dir", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("output_dir", options.output);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 3;
    const char * argv[] = { "program", "-ooutput_dir", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("output_dir", options.output);
    EXPECT_FALSE(options.help_needed);
  }

  // Long format
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 4;
    const char * argv[] = { "program", "--output", "output_dir", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("output_dir", options.output);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 3;
    const char * argv[] = { "program", "--output=output_dir", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("output_dir", options.output);
    EXPECT_FALSE(options.help_needed);
  }

  // None
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 2;
    const char * argv[] = { "program", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_EQ(NULL, options.output);
    EXPECT_FALSE(options.help_needed);
  }

  // Multiple
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 6;
    const char * argv[] = { "program", "-o", "dir1", "--output", "dir2", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("dir2", options.output);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_EQ(nullptr, options.output);

    int          argc   = 8;
    const char * argv[] = { "program", "--output=dir1", "-odir2",  "--output", "dir3",
                            "-o",      "dir4",          "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("dir4", options.output);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, IntOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_INT(param, count, 'c', "count", "Item number")

  // Short format.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    int          argc   = 4;
    const char * argv[] = { "program", "-c", "100", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(100, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    int          argc   = 3;
    const char * argv[] = { "program", "-c100", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(100, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  // Long format
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    int          argc   = 4;
    const char * argv[] = { "program", "--count", "100", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(100, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    int          argc   = 3;
    const char * argv[] = { "program", "--count=100", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(100, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  // None
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  // Multiple
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "-c", "100", "--count", "200", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(200, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "--count=100", "-c200", "--count",
                            "300",     "-c",          "400",   "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.count.used);
    EXPECT_EQ(400, options.count.value);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, IntOptionWithInvalidValues)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_INT(param, count, 'c', "count", "Item number")

  // Not a number.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "-cX", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Trailing non-digits.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "-c100Z", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Overflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "--count", "10000000000000000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Underflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.count.used);
    EXPECT_EQ(0, options.count.value);

    const char * argv[] = { "program", "--count", "-10000000000000000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, DoubleOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_DOUBLE(param, scale, 's', "scale", "Affine scale")

  // Short format.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    int          argc   = 4;
    const char * argv[] = { "program", "-s", "1.234", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(1.234, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    int          argc   = 3;
    const char * argv[] = { "program", "-s-1.234", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(-1.234, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  // Long format
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    int          argc   = 4;
    const char * argv[] = { "program", "--scale", "+1.234", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(1.234, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    int          argc   = 3;
    const char * argv[] = { "program", "--scale=-1.234", "argument" };
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(-1.234, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  // None
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  // Multiple
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "-s", "1.234", "--scale", "2.345", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(2.345, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }

  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "--scale=1.234", "-s2.345", "--scale", "3.456",
                            "-s",      "4.567",         "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_TRUE(options.scale.used);
    EXPECT_DOUBLE_EQ(4.567, options.scale.value);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, DoubleOptionWithInvalidValues)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param) ARGPARSE_OPTION_DOUBLE(param, scale, 's', "scale", "Affine scale")

  // Not a number.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "-sX", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Trailing non-digits.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "-s100Z", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Positive overflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "--scale", "1e2000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Negative overflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "--count", "-1e2000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Positive underflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "--scale", "1e-2000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }

  // Negative underflow
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);
    EXPECT_FALSE(options.scale.used);
    EXPECT_DOUBLE_EQ(0., options.scale.value);

    const char * argv[] = { "program", "--count", "-1e-2000", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, HelpOption)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param)                                                                          \
  ARGPARSE_OPTION_FLAG(param, my_flag, 'f', "flag", "My flag")                                     \
  ARGPARSE_OPTION_STRING(param, my_string, 's', "str", "My string")

  // No help.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_FALSE(options.help_needed);
  }

  // One --help with random unknown options that are ignored.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "--help", "--foo", "--bar=BAR", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_TRUE(options.help_needed);
  }

  // Same, but with --help at the end.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "--foo", "--bar=BAR", "argument", "--help" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_FALSE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_TRUE(options.help_needed);
  }

  // --help is ignored if argument to previous string option!
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "--str", "--help", "argument" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(2, argc);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("--help", options.my_string);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, DashDashAsSeparator)
{
#undef MY_OPTIONS
#define MY_OPTIONS(param)                                                                          \
  ARGPARSE_OPTION_FLAG(param, my_flag, 'f', "flag", "My flag")                                     \
  ARGPARSE_OPTION_STRING(param, my_string, 's', "str", "My string")

  // No remaining argument after --.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "argument", "--" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_FALSE(options.help_needed);
  }

  // Anything after -- is kept but not processed
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "argument", "--", "--flag" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 3);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("--flag", argv[2]);
    EXPECT_FALSE(options.my_flag);
    EXPECT_FALSE(options.help_needed);
  }

  // --help after -- is ignored as well.
  {
    ARGPARSE_DEFINE_OPTIONS_STRUCT(options, MY_OPTIONS);

    const char * argv[] = { "program", "argument", "--", "--help" };
    int          argc   = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(ARGPARSE_PARSE_ARGS(argc, argv, options, MY_OPTIONS));
    EXPECT_EQ(argc, 3);
    EXPECT_STREQ("program", argv[0]);
    EXPECT_STREQ("argument", argv[1]);
    EXPECT_STREQ("--help", argv[2]);
    EXPECT_FALSE(options.help_needed);
  }
}

TEST(argparse, HelpMessage)
{
  static const char kLongDescription[] =
    "A very long description text that "
    "should easily be split over multiple lines when unit-testing the arg_parse "
    "help message output.";

#undef MY_OPTIONS
#define MY_OPTIONS(param)                                                                          \
  ARGPARSE_OPTION_FLAG(param, my_flag, 'f', "flag", "My flag")                                     \
  ARGPARSE_OPTION_STRING(param, my_string, 's', "str", "My string")                                \
  ARGPARSE_OPTION_STRING(param, my_string2, 0, "long-option-name", "But short description")        \
  ARGPARSE_OPTION_STRING(param, my_string3, 'x', NULL, kLongDescription)

  // Long option name pushes the second column to 16 + long description
  // text split over multiple lines. One long option without a short char,
  // one short char option without a long name.
  {
    TestHelpOutput output;
    TEST_HELP_OUTPUT_GET(output, "mytest", "My small test", MY_OPTIONS);

    const char kExpected[] = "Usage: mytest [options] ...\n\n"
                             "My small test\n\n"
                             "  -f, --flag        My flag\n"
                             "  -s, --str=STR     My string\n"
                             "  --long-option-name=LONG_OPTION_NAME\n"
                             "                    But short description\n"
                             "\n"
                             "  -x                A very long description text that should\n"
                             "                    easily be split over multiple lines when\n"
                             "                    unit-testing the arg_parse help message\n"
                             "                    output.\n"
                             "\n"
                             "  -?, --help        Print help\n";

    EXPECT_STREQ(kExpected, output.text().c_str());
  }

#undef MY_OPTIONS
#define MY_OPTIONS(param)                                                                          \
  ARGPARSE_OPTION_FLAG(param, my_flag, 'f', "flag", "My flag")                                     \
  ARGPARSE_OPTION_STRING(param, my_string, 's', "str", "My string")                                \
  ARGPARSE_OPTION_STRING(param, my_string3, 'x', NULL, kLongDescription)

  // Same as above without the long option name, used to verify that
  // the second column is now smaller, and that the line split is different
  // since there is some more space available.
  {
    TestHelpOutput output;
    TEST_HELP_OUTPUT_GET(output, "mytest", "My small test", MY_OPTIONS);

    const char kExpected[] = "Usage: mytest [options] ...\n\n"
                             "My small test\n\n"
                             "  -f, --flag     My flag\n"
                             "  -s, --str=STR  My string\n"
                             "  -x             A very long description text that should\n"
                             "                 easily be split over multiple lines when\n"
                             "                 unit-testing the arg_parse help message output.\n"
                             "\n"
                             "  -?, --help     Print help\n";

    EXPECT_STREQ(kExpected, output.text().c_str());
  }
}

}  // namespace
