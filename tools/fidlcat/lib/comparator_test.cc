// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <array>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

namespace fidlcat {

class TestComparator : public Comparator {
 public:
  TestComparator(std::string_view expected_output) : Comparator(os_) {
    SetExpectedOutput(expected_output);
  }
  std::string output_string() {
    std::string res = os_.str();
    os_.clear();
    os_.str("");
    return res;
  }

  // We use a null terminated string for easy use in ASSERT_STREQ.
  std::string GetNextMessage() { return std::string(GetNextExpectedMessage()); }
  std::string GetMessageAsStr(std::string_view messages, size_t* number_char_processed) {
    return std::string(GetMessage(messages, number_char_processed));
  }

  bool SameProcessNamePidTidTest(std::string* actual, const std::string& expected) {
    return SameProcessNamePidTid(actual, expected);
  }
  bool CouldReplaceAllHandles(std::string* actual, const std::string& expected) {
    std::string handle1 = "handle: ";
    std::string handle2 = "handle = ";
    return CouldReplaceHandles(actual, expected, handle1) &&
           CouldReplaceHandles(actual, expected, handle2);
  }

 private:
  std::ostringstream os_;
};

// Test checking that GetMessage works: it returns one syscall input or output per call.
TEST(Comparator, GetMessage) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 28777:28779 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)

echo_client 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 6c0e0387
  }
)";
  std::string empty("");
  fidlcat::TestComparator comparator(empty);
  std::string message1 = "echo_client 28777:28779 zx_channel_create(options:uint32: 0)\n";
  std::string message2 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  std::string message3 =
      R"(echo_client 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 6c0e0387
  }
)";
  size_t number_char_processed = 0;
  ASSERT_STREQ(comparator.GetMessageAsStr(messages, &number_char_processed).c_str(),
               message1.c_str());
  ASSERT_EQ(number_char_processed, messages.find(message1) + message1.length());
  messages = messages.substr(number_char_processed);
  ASSERT_STREQ(comparator.GetMessageAsStr(messages, &number_char_processed).c_str(),
               message2.c_str());
  ASSERT_EQ(number_char_processed, message2.length());
  messages = messages.substr(number_char_processed);
  ASSERT_STREQ(comparator.GetMessageAsStr(messages, &number_char_processed).c_str(),
               message3.c_str());
  // + 1 for the ignored \n before message 3
  ASSERT_EQ(number_char_processed, message3.length() + 1);
}

// Test checking that GetNextExpectedMessage works: it returns one syscall input or output per call.
TEST(Comparator, GetNextMessage) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 28777:28779 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)

echo_client 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 6c0e0387
  }
)";
  fidlcat::TestComparator comparator(messages);
  std::string message1 = "echo_client 28777:28779 zx_channel_create(options:uint32: 0)\n";
  std::string message2 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  std::string message3 =
      R"(echo_client 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 6c0e0387
  }
)";
  ASSERT_STREQ(comparator.GetNextMessage().c_str(), message1.c_str());
  ASSERT_STREQ(comparator.GetNextMessage().c_str(), message2.c_str());
  ASSERT_STREQ(comparator.GetNextMessage().c_str(), message3.c_str());
}

TEST(Comparator, SameProcessNamePidTidTest) {
  std::string empty("");
  fidlcat::TestComparator comparator(empty);

  std::string actual0 = "compA 28777:28779 zx_channel_create(options:uint32: 0)\n";
  std::string expected0 = "compB 32809:32811 zx_channel_create(options:uint32: 0)\n";
  EXPECT_FALSE(comparator.SameProcessNamePidTidTest(&actual0, expected0));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different process names, expected compB actual was compA");

  std::string actual1 = "\necho_client 111:121 zx_channel_create(options:uint32: 0)\n";
  std::string expected1 = "\necho_client 011:021 zx_channel_create(options:uint32: 0)\n";
  // The comparator has no knowledge of process:tid match, so this should succeed
  EXPECT_TRUE(comparator.SameProcessNamePidTidTest(&actual1, expected1));
  EXPECT_STREQ(actual1.c_str(), expected1.c_str());

  std::string actual2 = "echo_client 111:121 zx_channel_create(options:uint32: 0)\n";
  std::string expected2 = "echo_client 019:021 zx_channel_create(options:uint32: 0)\n";
  // The comparator now knows that actual pid 111 is expected pid 011, so this should fail
  EXPECT_FALSE(comparator.SameProcessNamePidTidTest(&actual2, expected2));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different pids, actual pid 111 should be 11 in golden file but was 19");

  std::string actual3 = "echo_client 119:121 zx_channel_create(options:uint32: 0)\n";
  std::string expected3 = "echo_client 011:021 zx_channel_create(options:uint32: 0)\n";
  // The comparator now knows that expected pid 011 is actual pid 111, so this should fail
  EXPECT_FALSE(comparator.SameProcessNamePidTidTest(&actual3, expected3));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different pids, expected pid 11 should be 111 in this execution but was 119");

  std::string actual4 = "echo_client 111:121 zx_channel_create(options:uint32: 0)\n";
  std::string expected4 = "echo_client 011:029 zx_channel_create(options:uint32: 0)\n";
  EXPECT_FALSE(comparator.SameProcessNamePidTidTest(&actual4, expected4));
  EXPECT_STREQ(
      comparator.output_string().c_str(),
      "Different tids, actual pid:tid 111:121 should be 11:21 in golden file but was 11:29");

  std::string actual5 = "echo_client 111:129 zx_channel_create(options:uint32: 0)\n";
  std::string expected5 = "echo_client 011:021 zx_channel_create(options:uint32: 0)\n";
  EXPECT_FALSE(comparator.SameProcessNamePidTidTest(&actual5, expected5));
  EXPECT_STREQ(
      comparator.output_string().c_str(),
      "Different tids, expected pid:tid 11:21 should be 111:121 in this execution but was 111:129");
}

TEST(Comparator, CouldReplaceHandles) {
  std::string empty("");
  fidlcat::TestComparator comparator(empty);

  // Replacing after "handle: "
  std::string actual0 = "zx_channel_write(handle:handle: a0, options:uint32: 0)";
  std::string expected0 = "zx_channel_write(handle:handle: e0, options:uint32: 0)";
  EXPECT_TRUE(comparator.CouldReplaceAllHandles(&actual0, expected0));
  EXPECT_STREQ(actual0.c_str(), expected0.c_str());

  // Replacing after "handle = "
  std::string actual1 = "object: handle = a1";
  std::string expected1 = "object: handle = e1";
  EXPECT_TRUE(comparator.CouldReplaceAllHandles(&actual1, expected1));
  EXPECT_STREQ(actual1.c_str(), expected1.c_str());

  // If first occurence after "handle: ", saved for "handle = " as well
  std::string actual2 = "object: handle = a0";
  std::string expected2 = "object: handle = e2";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual2, expected2));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different handles, actual handle a0 should be e0 in golden file but was e2");

  // If first occurence after "handle = ", saved for "handle: " as well
  std::string actual3 = "zx_channel_write(handle:handle: a1, options:uint32: 0)";
  std::string expected3 = "zx_channel_write(handle:handle: e3, options:uint32: 0)";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual3, expected3));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different handles, actual handle a1 should be e1 in golden file but was e3");

  // And the other way round: wrong actual instead of wrong expected
  // If first occurence after "handle: ", saved for "handle = " as well
  std::string actual4 = "object: handle = a4";
  std::string expected4 = "object: handle = e0";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual4, expected4));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different handles, expected handle e0 should be a0 in this execution but was a4");

  // If first occurence after "handle = ", saved for "handle: " as well
  std::string actual5 = "zx_channel_write(handle:handle: a5, options:uint32: 0)";
  std::string expected5 = "zx_channel_write(handle:handle: e1, options:uint32: 0)";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual5, expected5));
  EXPECT_STREQ(comparator.output_string().c_str(),
               "Different handles, expected handle e1 should be a1 in this execution but was a5");
}

TEST(Comparator, CompareInputOutput) {
  std::string messages = R"(
echo_client 28777:28779 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)

echo_client 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 6c0e0387
  }
)";
  fidlcat::TestComparator comparator(messages);

  std::string input_1 = "echo_client 28777:28779 zx_channel_create(options:uint32: 0)\n";
  comparator.CompareInput(input_1);
  ASSERT_STREQ(comparator.output_string().c_str(), "");

  std::string output_1 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  comparator.CompareOutput(output_1);
  ASSERT_STREQ(comparator.output_string().c_str(), "");

  std::string input_2 =
      "\nAAA 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)\n";
  comparator.CompareInput(input_2);
  ASSERT_STREQ(comparator.output_string().c_str(),
               "Different process names, expected echo_client actual was AAA");

  // Comparison should have stopped now, and ignore next outputs
  std::string output_2 = "anything";
  comparator.CompareOutput(output_2);
  ASSERT_STREQ(comparator.output_string().c_str(), "");

  // Next inputs should be ignored as well
  std::string input_3 = "anything";
  comparator.CompareInput(input_3);
  ASSERT_STREQ(comparator.output_string().c_str(), "");
}

}  // namespace fidlcat
