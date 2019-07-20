// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/bugreport/command_line_options.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace bugreport {
namespace {

TEST(CommandLineOptionsTest, Default) {
  const char* argv[] = {"bugreport"};
  EXPECT_EQ(ParseModeFromArgcArgv(1, argv), Mode::DEFAULT);
}

TEST(CommandLineOptionsTest, Minimal) {
  const char* argv[] = {"bugreport", "--minimal"};
  EXPECT_EQ(ParseModeFromArgcArgv(2, argv), Mode::MINIMAL);
}

TEST(CommandLineOptionsTest, Help) {
  const char* argv[] = {"bugreport", "--help"};
  EXPECT_EQ(ParseModeFromArgcArgv(2, argv), Mode::HELP);
}

TEST(CommandLineOptionsTest, HelpAnywhere) {
  const char* argv[] = {"bugreport", "--irrelevant", "--help"};
  EXPECT_EQ(ParseModeFromArgcArgv(3, argv), Mode::HELP);
}

TEST(CommandLineOptionsTest, HelpAsPositionalArgument) {
  const char* argv[] = {"bugreport", "help"};
  EXPECT_EQ(ParseModeFromArgcArgv(2, argv), Mode::HELP);
}

TEST(CommandLineOptionsTest, HelpAsPositionalArgumentAnywhere) {
  const char* argv[] = {"bugreport", "--irrelevant", "help"};
  EXPECT_EQ(ParseModeFromArgcArgv(3, argv), Mode::HELP);
}

TEST(CommandLineOptionsTest, FailureUnknownOption) {
  const char* argv[] = {"bugreport", "--unknown"};
  EXPECT_EQ(ParseModeFromArgcArgv(2, argv), Mode::FAILURE);
}

TEST(CommandLineOptionsTest, FailureUnknownPositionalArgument) {
  const char* argv[] = {"bugreport", "unknown"};
  EXPECT_EQ(ParseModeFromArgcArgv(2, argv), Mode::FAILURE);
}

}  // namespace

// Pretty-prints Mode in gTest matchers instead of the default byte string in case of failed
// expectations.
void PrintTo(const Mode& mode, std::ostream* os) {
  switch (mode) {
    case Mode::FAILURE:
      *os << "FAILURE";
      return;
    case Mode::HELP:
      *os << "HELP";
      return;
    case Mode::MINIMAL:
      *os << "MINIMAL";
      return;
    case Mode::DEFAULT:
      *os << "DEFAULT";
      return;
  }
}

}  // namespace bugreport
}  // namespace fuchsia
