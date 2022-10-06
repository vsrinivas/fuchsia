// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_sequence.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class CommandSequence : public ConsoleTest {
 public:
  // Returns a command context that writes its outputs and error into the given pointers on
  // success. The called boolean is set to true on completion. The pointers must remain valid longer
  // than the context.
  fxl::RefPtr<CommandContext> MakeTestContext(bool* called, std::string* output, Err* first_error) {
    return fxl::MakeRefCounted<OfflineCommandContext>(
        &console(),
        [called, output, first_error](OutputBuffer in_output, std::vector<Err> in_errors) {
          *called = true;
          *output = in_output.AsString();

          // Save the first error.
          if (!in_errors.empty()) {
            *first_error = in_errors[0];
          } else {
            *first_error = Err();
          }
        });
  }
};

}  // namespace

TEST_F(CommandSequence, Empty) {
  bool called = false;
  std::string output;
  Err error;
  RunCommandSequence(&console(), {}, MakeTestContext(&called, &output, &error));
  EXPECT_TRUE(called);
  EXPECT_TRUE(output.empty());
  EXPECT_TRUE(error.ok());
}

TEST_F(CommandSequence, Success) {
  // These commands were picked because they don't require a connection.
  bool called = false;
  std::string output;
  Err error;
  RunCommandSequence(&console(), {"break main", "get show-stdout"},
                     MakeTestContext(&called, &output, &error));

  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);

  // This just searches for some keywords we know are in the output, so we don't depend on the
  // exact wording of the messages.
  EXPECT_NE(std::string::npos, output.find("Created Breakpoint")) << "Got: " << output;
  EXPECT_NE(std::string::npos, output.find("show-stdout")) << "Got: " << output;
}

TEST_F(CommandSequence, Error) {
  // These commands were picked because they don't require a connection.
  bool called = false;
  std::string output;
  Err error;
  RunCommandSequence(&console(), {"floofbunny", "break main"},
                     MakeTestContext(&called, &output, &error));

  loop().RunUntilNoTasks();

  EXPECT_TRUE(called);

  // There should be no real output (the valid "break" command should never have run).
  EXPECT_TRUE(output.empty()) << output;

  ASSERT_EQ("The string \"floofbunny\" is not a valid verb.", error.msg());
}

}  // namespace zxdb
