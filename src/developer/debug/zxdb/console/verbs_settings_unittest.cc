// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

class VerbsSettingsTest : public RemoteAPITest {
 public:
  VerbsSettingsTest() : RemoteAPITest() {}

  // The MockConsole must live in a smaller scope than the Session/System managed by the
  // RemoteAPITest's SetUp()/TearDown() functions.
  void SetUp() override {
    RemoteAPITest::SetUp();
    console_ = std::make_unique<MockConsole>(&session());
  }
  void TearDown() override {
    console_.reset();
    RemoteAPITest::TearDown();
  }

  MockConsole& console() { return *console_; }

  // Runs an input command and returns the text synchronously reported from it.
  std::string DoInput(const std::string& input) {
    console_->ProcessInputLine(input);

    auto event = console_->GetOutputEvent();
    EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);

    return event.output.AsString();
  }

  // "get" output returns the help text which we don't want to hardcode here. This function extracts
  // just the values from the given output message. It will not have a newline terminator.
  std::string ExtractValuesFromGet(const std::string& input) {
    std::string values_heading = "Value(s):\n";

    size_t heading_index = input.find(values_heading);
    if (heading_index == std::string::npos) {
      EXPECT_NE(heading_index, std::string::npos);
      return std::string();
    }
    size_t value_index = heading_index + values_heading.size();

    // The end is the next blank line.
    size_t end_index = input.find("\n\n", value_index);
    if (end_index == std::string::npos)
      end_index = input.size();

    return input.substr(value_index, end_index - value_index);
  }

 private:
  std::unique_ptr<MockConsole> console_;
};

}  // namespace

TEST_F(VerbsSettingsTest, GetSet) {
  console().Clear();

  // "get" with no input.
  EXPECT_EQ("<empty>", ExtractValuesFromGet(DoInput("get build-dirs")));

  // Process qualified set.
  EXPECT_EQ(
      "Set value(s) for process 1:\n"
      "• prdir\n",
      DoInput("pr set build-dirs prdir"));

  // Both the unqualified and process-qualified one should get it,
  EXPECT_EQ("• prdir", ExtractValuesFromGet(DoInput("get build-dirs")));
  EXPECT_EQ("• prdir", ExtractValuesFromGet(DoInput("process get build-dirs")));

  // Globally qualified set.
  EXPECT_EQ(
      "Set value(s) system-wide:\n"
      "• gldir\n",
      DoInput("global set build-dirs gldir"));

  // The globally qualified one should return it, but the unqualified one should return the process
  // since it's more specific.
  EXPECT_EQ("• prdir", ExtractValuesFromGet(DoInput("process get build-dirs")));
  EXPECT_EQ("• gldir", ExtractValuesFromGet(DoInput("global get build-dirs")));
  EXPECT_EQ("• prdir", ExtractValuesFromGet(DoInput("get build-dirs")));

  // Unqualified set.
  EXPECT_EQ(
      "Set value(s) system-wide:\n"
      "• gldir2\n",
      DoInput("set build-dirs gldir2"));

  // Append.
  EXPECT_EQ(
      "Added value(s) system-wide:\n"
      "• gldir2\n"
      "• gldir3\n",
      DoInput("set build-dirs += gldir3"));

  EXPECT_EQ(
      "• gldir2\n"
      "• gldir3",
      ExtractValuesFromGet(DoInput("global get build-dirs")));
  EXPECT_EQ("• prdir", ExtractValuesFromGet(DoInput("get build-dirs")));
}

}  // namespace zxdb
