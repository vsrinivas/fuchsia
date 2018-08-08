// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "gtest/gtest.h"

namespace zxdb {

using Option = fxl::CommandLine::Option;
// Macro for doing the ProcessFunction signature. We just want to know if it
// was called.
#define CALLED_FUNC(func_name, called_bool)                     \
  FlagRecord::ProcessFunction func_name =                       \
      [called = &called_bool](const Option&, const FlagRecord&, \
                              std::vector<Action>* actions) {   \
        *called = true;                                         \
        actions->push_back({});                                 \
        return Err();                                           \
      }

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
void ReplaceFlags() {
  std::vector<FlagRecord> mock_flags;
  mock_flags.push_back(kNoArgument);
  mock_flags.push_back(kArgument);
  mock_flags.push_back(kDefaultArgument);

  // Setup the static flags
  OverrideFlags(mock_flags);
}

class FlagsTest : public testing::Test {
 protected:
  FlagsTest() {
    // We override flags
    ReplaceFlags();
  }

  ActionFlow flow;
};

TEST_F(FlagsTest, NoArgument) {
  // SETUP
  Err err;
  FlagProcessResult flag_res;
  std::vector<Action> actions;

  // Wrong argument
  std::string call = fxl::StringPrintf("--%s=AAA", kNoArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kError);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(),
            fxl::StringPrintf("Flag \"%s\" doesn't receive arguments.",
                              kNoArgumentLongForm));

  // Normal
  actions.clear();
  call = fxl::StringPrintf("--%s", kNoArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kContinue);
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST_F(FlagsTest, Argument) {
  // SETUP
  Err err;
  FlagProcessResult flag_res;
  std::vector<Action> actions;

  // Argument needed
  std::string call = fxl::StringPrintf("--%s", kArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kError);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), fxl::StringPrintf("Flag \"%s\" expects an argument.",
                                         kArgumentLongForm));
  EXPECT_TRUE(actions.empty());

  // Expected
  actions.clear();
  call = fxl::StringPrintf("--%s=AAA", kArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kContinue);
  EXPECT_FALSE(err.has_error());
}

TEST_F(FlagsTest, DefaultArgument) {
  // SETUP
  Err err;
  FlagProcessResult flag_res;
  std::vector<Action> actions;

  // Default
  std::string call = fxl::StringPrintf("--%s", kDefaultArgumentLongForm);
  auto cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kContinue);
  EXPECT_FALSE(err.has_error());

  // With argument
  actions.clear();
  call = fxl::StringPrintf("--%s=AAA", kDefaultArgumentLongForm);
  cmd_line = fxl::CommandLineFromInitializerList({"bin", call.c_str()});
  flag_res = ProcessCommandLine(cmd_line, &err, &actions);

  EXPECT_EQ(flag_res, FlagProcessResult::kContinue);
  EXPECT_FALSE(err.has_error());
}

}  // namespace zxdb
