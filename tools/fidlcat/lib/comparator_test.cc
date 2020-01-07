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
    ParseGolden(expected_output);
  }
  std::string output_string() {
    std::string res = os_.str();
    os_.clear();
    os_.str("");
    return res;
  }

  // We use a null terminated string for easy use in ASSERT_EQ.
  std::string GetMessageAsStr(std::string_view messages, size_t* number_char_processed) {
    return std::string(GetMessage(messages, number_char_processed));
  }

  std::unique_ptr<Message> GetNextExpectedMessageTest(uint64_t pid, uint64_t tid) {
    return GetNextExpectedMessage(pid, tid);
  }

  bool CouldReplaceAllHandles(std::string* actual, const std::string& expected) {
    std::string handle1 = "handle: ";
    std::string handle2 = "handle = ";
    return CouldReplaceHandles(actual, expected, handle1) &&
           CouldReplaceHandles(actual, expected, handle2);
  }

  // Used to access directly the messages_ map, removes the returned message from the map.
  // If there is no message in the map for the given (pid, tid), returns a null pointer.
  std::unique_ptr<Message> GetMessageFromExpectedPidTid(uint64_t pid, uint64_t tid) {
    std::pair<uint64_t, uint64_t> pid_tid(pid, tid);
    if (messages_.find(pid_tid) == messages_.end()) {
      return nullptr;
    }
    if (messages_[pid_tid].empty()) {
      return nullptr;
    }
    std::unique_ptr<Message> result = std::move(messages_[pid_tid].front());
    messages_[pid_tid].pop_front();
    return result;
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
  TestComparator comparator("");
  size_t number_char_processed = 0;
  ASSERT_EQ(comparator.GetMessageAsStr(messages, &number_char_processed), message1);
  ASSERT_EQ(number_char_processed, messages.find(message1) + message1.length());
  messages = messages.substr(number_char_processed);
  ASSERT_EQ(comparator.GetMessageAsStr(messages, &number_char_processed), message2);
  ASSERT_EQ(number_char_processed, message2.length());
  messages = messages.substr(number_char_processed);
  ASSERT_EQ(comparator.GetMessageAsStr(messages, &number_char_processed), message3);
  // + 1 for the ignored \n before message 3
  ASSERT_EQ(number_char_processed, message3.length() + 1);
}

// Test checking that ParseGolden works: it updates the messages map, as well as the order of
// appearance of pid/tids
TEST(Comparator, ParseGolden) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)

echo_client 2:21 zx_channel_write(handle:handle: 221a, options:uint32: 0)

echo_client 1:12 zx_channel_write(handle:handle: 112a, options:uint32: 0)

echo_client 2:21   -> ZX_OK (out0:handle: 221b, out1:handle: 221c)
)";
  TestComparator comparator(messages);

  // pids are in the right order
  std::deque<uint64_t> pids = comparator.pids_by_order_of_appearance();
  ASSERT_EQ(pids.size(), 2u);
  ASSERT_EQ(pids.front(), 1u);
  pids.pop_front();
  ASSERT_EQ(pids.front(), 2u);

  // for a given pid, tids are in the right order
  std::map<uint64_t, std::deque<uint64_t>> tids = comparator.tids_by_order_of_appearance();
  ASSERT_NE(tids.find(1), tids.end());
  ASSERT_EQ(tids[1].size(), 2u);
  ASSERT_EQ(tids[1].front(), 11u);
  tids[1].pop_front();
  ASSERT_EQ(tids[1].front(), 12u);
  ASSERT_NE(tids.find(2), tids.end());
  ASSERT_EQ(tids[2].size(), 1u);
  ASSERT_EQ(tids[2].front(), 21u);

  // for a given (pid, tid), messages are in the right order, and properly stripped of process_name
  // and pid:tid
  std::string message1 = "zx_channel_create(options:uint32: 0)\n";
  std::string message2 = "  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)\n";
  std::unique_ptr<Message> message_in_map = comparator.GetMessageFromExpectedPidTid(1, 11);
  ASSERT_NE(message_in_map, nullptr);
  ASSERT_EQ(message_in_map->message, message1);
  ASSERT_EQ(message_in_map->process_name, std::string("echo_client"));
  message_in_map = comparator.GetMessageFromExpectedPidTid(1, 11);
  ASSERT_NE(message_in_map, nullptr);
  ASSERT_EQ(message_in_map->message, message2);
  ASSERT_EQ(message_in_map->process_name, std::string("echo_client"));

  message1 = "zx_channel_write(handle:handle: 221a, options:uint32: 0)\n";
  message2 = "  -> ZX_OK (out0:handle: 221b, out1:handle: 221c)\n";
  message_in_map = comparator.GetMessageFromExpectedPidTid(2, 21);
  ASSERT_NE(message_in_map, nullptr);
  ASSERT_EQ(message_in_map->message, message1);
  message_in_map = comparator.GetMessageFromExpectedPidTid(2, 21);
  ASSERT_NE(message_in_map, nullptr);
  ASSERT_EQ(message_in_map->message, message2);
}

// Test checking that GetNextExpectedMessage works: it gets unknown pids/tids in the right order,
// while matching known ones correctly
TEST(Comparator, GetNextExpectedMessage) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)

echo_client 2:21 zx_channel_write(handle:handle: 221a, options:uint32: 0)

echo_client 1:12 zx_channel_write(handle:handle: 112a, options:uint32: 0)

echo_client 2:21   -> ZX_OK (out0:handle: 221b, out1:handle: 221c)
)";
  TestComparator comparator(messages);

  // In this test, we call on GetNextExpectedMessage as if the actual output was the following. Note
  // that only the order of pids, tids per pid and messages per tid are respected here.
  //  Launched run fuchsia-pkg
  //  echo_client 1:11 zx_channel_create(options:uint32: 0)
  //  echo_client 1:12 zx_channel_write(handle:handle: 112a, options:uint32: 0)
  //  echo_client 2:21 zx_channel_write(handle:handle: 221a, options:uint32: 0)
  //  echo_client 1:11   -> ZX_OK (out0:handle:111a, out1:handle: 111b)
  //  echo_client 2:21   -> ZX_OK (out0:handle: 221b, out1:handle: 221c)
  // We use the following pids:tids mapping: expected pid:tid -> actual pid:tid
  //  1:11 -> 3:31
  //  1:12 -> 3:32
  //  2:21 -> 4:41

  std::unique_ptr<Message> result = comparator.GetNextExpectedMessageTest(3, 31);
  ASSERT_EQ("zx_channel_create(options:uint32: 0)\n", result->message);
  result = comparator.GetNextExpectedMessageTest(3, 32);
  ASSERT_EQ("zx_channel_write(handle:handle: 112a, options:uint32: 0)\n", result->message);
  result = comparator.GetNextExpectedMessageTest(4, 41);
  ASSERT_EQ("zx_channel_write(handle:handle: 221a, options:uint32: 0)\n", result->message);
  result = comparator.GetNextExpectedMessageTest(3, 31);
  ASSERT_EQ("  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)\n", result->message);
  result = comparator.GetNextExpectedMessageTest(4, 41);
  ASSERT_EQ("  -> ZX_OK (out0:handle: 221b, out1:handle: 221c)\n", result->message);

  // Now we check that the correspondance maps between pids and tids were properly updated
  std::map<uint64_t, uint64_t> expected_pids = comparator.expected_pids();
  ASSERT_NE(expected_pids.find(3), expected_pids.end());
  ASSERT_EQ(expected_pids[3], 1u);
  ASSERT_NE(expected_pids.find(4), expected_pids.end());
  ASSERT_EQ(expected_pids[4], 2u);

  std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> expected_pids_tids =
      comparator.expected_pids_tids();
  std::pair actual3_31(3, 31);
  ASSERT_NE(expected_pids_tids.find(actual3_31), expected_pids_tids.end());
  ASSERT_TRUE(expected_pids_tids[actual3_31] == (std::pair<uint64_t, uint64_t>(1, 11)));
  std::pair actual3_32(3, 32);
  ASSERT_NE(expected_pids_tids.find(actual3_32), expected_pids_tids.end());
  ASSERT_TRUE(expected_pids_tids[actual3_32] == (std::pair<uint64_t, uint64_t>(1, 12)));
  std::pair actual4_41(4, 41);
  ASSERT_NE(expected_pids_tids.find(actual4_41), expected_pids_tids.end());
  ASSERT_TRUE(expected_pids_tids[actual4_41] == (std::pair<uint64_t, uint64_t>(2, 21)));
}

TEST(Comparator, CouldReplaceHandles) {
  std::string empty("");
  fidlcat::TestComparator comparator(empty);

  // Replacing after "handle: "
  std::string actual0 = "zx_channel_write(handle:handle: a0, options:uint32: 0)";
  std::string expected0 = "zx_channel_write(handle:handle: e0, options:uint32: 0)";
  EXPECT_TRUE(comparator.CouldReplaceAllHandles(&actual0, expected0));
  EXPECT_EQ(actual0, expected0);

  // Replacing after "handle = "
  std::string actual1 = "object: handle = a1";
  std::string expected1 = "object: handle = e1";
  EXPECT_TRUE(comparator.CouldReplaceAllHandles(&actual1, expected1));
  EXPECT_EQ(actual1, expected1);

  // If first occurence after "handle: ", saved for "handle = " as well
  std::string actual2 = "object: handle = a0";
  std::string expected2 = "object: handle = e2";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual2, expected2));
  EXPECT_EQ("Different handles, actual handle a0 should be e0 in golden file but was e2",
            comparator.output_string());

  // If first occurence after "handle = ", saved for "handle: " as well
  std::string actual3 = "zx_channel_write(handle:handle: a1, options:uint32: 0)";
  std::string expected3 = "zx_channel_write(handle:handle: e3, options:uint32: 0)";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual3, expected3));
  EXPECT_EQ("Different handles, actual handle a1 should be e1 in golden file but was e3",
            comparator.output_string());

  // And the other way round: wrong actual instead of wrong expected
  // If first occurence after "handle: ", saved for "handle = " as well
  std::string actual4 = "object: handle = a4";
  std::string expected4 = "object: handle = e0";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual4, expected4));
  EXPECT_EQ("Different handles, expected handle e0 should be a0 in this execution but was a4",
            comparator.output_string());

  // If first occurence after "handle = ", saved for "handle: " as well
  std::string actual5 = "zx_channel_write(handle:handle: a5, options:uint32: 0)";
  std::string expected5 = "zx_channel_write(handle:handle: e1, options:uint32: 0)";
  EXPECT_FALSE(comparator.CouldReplaceAllHandles(&actual5, expected5));
  EXPECT_EQ("Different handles, expected handle e1 should be a1 in this execution but was a5",
            comparator.output_string());
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
  comparator.CompareInput(input_1, 1, 2);
  ASSERT_EQ("", comparator.output_string());

  std::string output_1 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  comparator.CompareOutput(output_1, 1, 2);
  ASSERT_EQ("", comparator.output_string());

  std::string input_2 =
      "\nAAA 28777:28779 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)\n";
  comparator.CompareInput(input_2, 1, 2);
  ASSERT_EQ(
      "Different process names for actual pid:tid 1:2, matched with expected pid:tid "
      "28777:28779, expected echo_client actual was AAA",
      comparator.output_string());

  // Comparison should have stopped now, and ignore next outputs
  std::string output_2 = "anything";
  comparator.CompareOutput(output_2, 1, 2);
  ASSERT_EQ("", comparator.output_string());

  // Next inputs should be ignored as well
  std::string input_3 = "anything";
  comparator.CompareInput(input_3, 1, 2);
  ASSERT_EQ("", comparator.output_string());
}

}  // namespace fidlcat
