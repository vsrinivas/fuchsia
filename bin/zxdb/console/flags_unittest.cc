// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "gtest/gtest.h"

namespace zxdb {

const char* kNoArgumentName = "k-no-argument-name";
const char* kNoArgumentLongForm = "k-no-argument-long-form";
const char* kNoArgumentLongHelp = "k-no-argument-long-help";
const char* kNoArgumentShortHelp = "k-no-argument-short-help";

FlagRecord kNoArgument =
    FlagRecord(kNoArgumentName, kNoArgumentLongForm, nullptr,
               kNoArgumentLongHelp, kNoArgumentShortHelp, nullptr, nullptr);

const char* kArgumentName = "k-argument-name";
const char* kArgumentLongForm = "k-argument-long-form";
const char* kArgumentLongHelp = "k-argument-long-help";
const char* kArgumentShortHelp = "k-argument-short-help";
const char* kArgumentArgumentName = "k-argument-argument-name";

FlagRecord kArgument =
    FlagRecord(kArgumentName, kArgumentLongForm, nullptr, kArgumentLongHelp,
               kArgumentShortHelp, kArgumentArgumentName, nullptr);

const char* kDefaultArgumentName = "k-default-argument-name";
const char* kDefaultArgumentLongForm = "k-default-argument-long-form";
const char* kDefaultArgumentLongHelp = "k-default-argument-long-help";
const char* kDefaultArgumentShortHelp = "k-default-argument-short-help";
const char* kDefaultArgumentArgumentName = "k-default-argument-argument-name";
const char* kDefaultArgumentDefaultArgument =
    "k-default-argument-default-argument";

FlagRecord kDefaultArgument =
    FlagRecord(kDefaultArgumentName, kDefaultArgumentLongForm, nullptr,
               kDefaultArgumentLongHelp, kDefaultArgumentShortHelp,
               kDefaultArgumentArgumentName, kDefaultArgumentDefaultArgument);

// Return value used to set up a static variable to ensure this function is
// only called once.
int InitializeMockFlags() {
  std::vector<FlagRecord> mock_flags;
  mock_flags.push_back(kNoArgument);
  mock_flags.push_back(kArgument);
  mock_flags.push_back(kDefaultArgument);

  // Setup the static flags
  SetupFlagsOnce(&mock_flags);

  return 0;
}

class FlagTest : public testing::Test {
 protected:
  FlagTest() {
    // Be sure that the flags are initialized before the tests
    // We only need to run this once
    static auto __tmp = InitializeMockFlags();
    (void)__tmp;  // No un-used warning
  }
};

TEST_F(FlagTest, NoArgument) {
  bool quit;
  // Normal
  std::string out;
  std::string call = fxl::StringPrintf("--%s", kNoArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});

  Err err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_FALSE(err.has_error());
  EXPECT_FALSE(quit);

  // No Argument
  call = fxl::StringPrintf("--%s=AAA", kNoArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(),
            fxl::StringPrintf("Flag \"%s\" doesn't receive arguments.",
                              kNoArgumentLongForm));
  EXPECT_FALSE(quit);
}

TEST_F(FlagTest, Argument) {
  bool quit;
  std::string out;
  std::string call = fxl::StringPrintf("--%s", kArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});

  Err err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), fxl::StringPrintf("Flag \"%s\" expects an argument.",
                                         kArgumentLongForm));
  EXPECT_FALSE(quit);

  call = fxl::StringPrintf("--%s=AAA", kArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_FALSE(err.has_error());
  EXPECT_FALSE(quit);
}

TEST_F(FlagTest, DefaultArgument) {
  bool quit;
  std::string out;
  std::string call = fxl::StringPrintf("--%s", kDefaultArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});

  Err err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_FALSE(err.has_error());
  EXPECT_FALSE(quit);

  call = fxl::StringPrintf("--%s=AAA", kDefaultArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  err = ProcessCommandLine(cmd_line, &out, &quit);
  EXPECT_FALSE(err.has_error());
  EXPECT_FALSE(quit);
}

TEST_F(FlagTest, Help) {
  bool quit;
  std::string out;
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", "--help"});
  Err err = ProcessCommandLine(cmd_line, &out, &quit);

  EXPECT_FALSE(err.has_error());
  EXPECT_TRUE(quit);

  std::stringstream ss;
  ss << "Usage: zxdb [OPTION ...]\n\n"
     << "options:\n";
  ss << "--" << kNoArgumentLongForm << ": " << kNoArgumentShortHelp
     << std::endl;

  ss << "--" << kArgumentLongForm << " <" << kArgumentArgumentName
     << ">: " << kArgumentShortHelp << std::endl;

  ss << "--" << kDefaultArgumentLongForm << " [" << kDefaultArgumentArgumentName
     << "]: " << kDefaultArgumentShortHelp << std::endl;

  EXPECT_EQ(out, ss.str());
}

TEST_F(FlagTest, SpecificHelp) {
  bool quit;
  std::string out;

  std::string call = fxl::StringPrintf("--help=%s", kDefaultArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  Err err = ProcessCommandLine(cmd_line, &out, &quit);

  EXPECT_FALSE(err.has_error());
  EXPECT_TRUE(quit);

  std::stringstream ss;
  ss << kDefaultArgumentName << std::endl;
  ss << "Usage: "
     << "--" << kDefaultArgumentLongForm << " [" << kDefaultArgumentArgumentName
     << "]\n\n";
  ss << kDefaultArgumentLongHelp << std::endl;

  EXPECT_EQ(out, ss.str());
}

}  // namespace zxdb
