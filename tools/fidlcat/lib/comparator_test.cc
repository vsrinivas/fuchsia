// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <array>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "tools/fidlcat/lib/message_graph.h"

namespace fidlcat {

class TestComparator : public Comparator {
 public:
  TestComparator(std::string_view expected_output) : Comparator(os_) {
    ParseGolden(expected_output);
  }

  TestComparator() : Comparator(os_) {}

  std::shared_ptr<GoldenMessageNode> InsertMessage(std::string process_name, uint64_t pid,
                                                   uint64_t tid, std::string_view cur_msg) {
    return golden_message_graph_.InsertMessage(process_name, pid, tid, cur_msg);
  }

  bool UniqueMatchToGoldenTest(std::shared_ptr<ActualMessageNode> actual_message_node) {
    return UniqueMatchToGolden(actual_message_node);
  }

  bool PropagateMatchTest(std::shared_ptr<ActualNode> actual_node) {
    return PropagateMatch(actual_node, false);
  }

  bool ReversePropagateMatchTest(std::shared_ptr<ActualNode> actual_node) {
    return ReversePropagateMatch(actual_node);
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

  const GoldenMessageGraph& golden_message_graph() { return golden_message_graph_; }

 private:
  std::ostringstream os_;
};

// Test checking that GetMessage works: it returns one syscall input or output per call.
TEST(Comparator, GetMessage) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 28777:28779 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)

echo_client 287283:287285 zx_channel_write(handle:handle: b84b2b47, options:uint32: 0)
  sent request fuchsia.sys/Launcher.CreateComponent = {
    launch_info: fuchsia.sys/LaunchInfo = {
      flat_namespace: fuchsia.sys/FlatNamespace = null
      additional_services: fuchsia.sys/ServiceList = null
    }
    controller: handle = b6bb28d3
  }

josh 30539:30541 zx_channel_call(handle:handle: e1d252ab, options:uint32: 0)
  sent request fuchsia.io/File.Write = { data: vector<uint8> = [


  ] }
  -> ZX_OK
    received response fuchsia.io/File.Read = {
      s: int32 = 0
      data: vector<uint8> = [
        // Copyright 2019 The Fuchsia Authors. All rights reserved.
        // Use of this source code is governed by a BSD-style license that can be
        // found in the LICENSE file.

        /**
         * @fileoverview Utilities for printing objects nicely.
         */
      ]
    }
)";
  std::string message1 = "echo_client 28777:28779 zx_channel_create(options:uint32: 0)\n";
  std::string message2 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  std::string message3 =
      R"(echo_client 287283:287285 zx_channel_write(handle:handle: b84b2b47, options:uint32: 0)
  sent request fuchsia.sys/Launcher.CreateComponent = {
    launch_info: fuchsia.sys/LaunchInfo = {
      flat_namespace: fuchsia.sys/FlatNamespace = null
      additional_services: fuchsia.sys/ServiceList = null
    }
    controller: handle = b6bb28d3
  }
)";
  std::string message4 =
      R"(josh 30539:30541 zx_channel_call(handle:handle: e1d252ab, options:uint32: 0)
  sent request fuchsia.io/File.Write = { data: vector<uint8> = [


  ] }
)";
  std::string message5 =
      R"(  -> ZX_OK
    received response fuchsia.io/File.Read = {
      s: int32 = 0
      data: vector<uint8> = [
        // Copyright 2019 The Fuchsia Authors. All rights reserved.
        // Use of this source code is governed by a BSD-style license that can be
        // found in the LICENSE file.

        /**
         * @fileoverview Utilities for printing objects nicely.
         */
      ]
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
  messages = messages.substr(number_char_processed);
  ASSERT_EQ(comparator.GetMessageAsStr(messages, &number_char_processed), message4);
  // + 1 for the ignored \n before message 4
  ASSERT_EQ(number_char_processed, message4.length() + 1);
  messages = messages.substr(number_char_processed);
  ASSERT_EQ(comparator.GetMessageAsStr(messages, &number_char_processed), message5);
  ASSERT_EQ(number_char_processed, message5.length());
}

// Checks that the golden graph construction is correct, even for interleaved syscalls, or syscalls
// without a return.
TEST(Comparator, ParseGolden) {
  std::string messages = R"(
Launched run fuchsia-pkg
echo_client 1:1 zx_process_exit(retcode:uint64: 0)

echo_client 1:1 zx_channel_create(options:uint32: 0)

echo_client 1:2 zx_channel_write(handle:handle: a1, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = a2
  }
  -> ZX_OK

echo_client 1:1   -> ZX_OK (out0:handle: a1, out1:handle: a2)
)";
  std::string message1 = "zx_process_exit(retcode:uint64: 0)\n";
  std::string message2 = "zx_channel_create(options:uint32: 0)\n";
  std::string message3 =
      R"(zx_channel_write(handle:handle: 0, options:uint32: 0)
  sent request fuchsia.io/Directory.Open = {
    flags: uint32 = 3
    mode: uint32 = 493
    path: string = "fuchsia.sys.Launcher"
    object: handle = 1
  }
)";
  std::string message4 = "  -> ZX_OK\n";
  std::string message5 = "  -> ZX_OK (out0:handle: 0, out1:handle: 1)\n";
  TestComparator comparator(messages);
  GoldenMessageGraph golden_message_graph = comparator.golden_message_graph();

  // Check tid to pid dependencies.
  auto tid_node1 = golden_message_graph.get_tid_node(1);
  auto tid_node2 = golden_message_graph.get_tid_node(2);
  auto pid_node1 = golden_message_graph.get_pid_node(1);
  ASSERT_EQ(tid_node1->get_dependency_by_type(DependencyType(kTidNode, kPidNode)), pid_node1);
  ASSERT_EQ(tid_node2->get_dependency_by_type(DependencyType(kTidNode, kPidNode)), pid_node1);

  // Check dependencies for messages.
  auto message_nodes = golden_message_graph.message_nodes();

  // message1
  auto message_node1 = message_nodes[message1][0];
  ASSERT_EQ(message_node1->get_dependency_by_type(DependencyType(kMessageNode, kTidNode)),
            tid_node1);

  // message2
  auto message_node2 = message_nodes[message2][0];
  ASSERT_EQ(message_node2->get_dependency_by_type(DependencyType(kMessageNode, kTidNode)),
            tid_node1);

  // message 3
  auto message_node3 = message_nodes[message3][0];
  auto handle_nodea1 = golden_message_graph.get_handle_node(0xa1);
  auto handle_nodea2 = golden_message_graph.get_handle_node(0xa2);
  ASSERT_EQ(message_node3->get_dependency_by_type(DependencyType(kZxWriteMessageNode, kTidNode)),
            tid_node2);
  ASSERT_EQ(
      message_node3->get_dependency_by_type(DependencyType(kZxWriteMessageNode, kHandleNode + 0)),
      handle_nodea1);
  ASSERT_EQ(
      message_node3->get_dependency_by_type(DependencyType(kZxWriteMessageNode, kHandleNode + 1)),
      handle_nodea2);

  // message 4
  auto message_node4 = message_nodes[message4][0];
  ASSERT_EQ(message_node4->get_dependency_by_type(DependencyType(kMessageNode, kTidNode)),
            tid_node2);
  ASSERT_EQ(message_node4->get_dependency_by_type(DependencyType(kMessageNode, kMessageInputNode)),
            message_node3);

  // message 5
  auto message_node5 = message_nodes[message5][0];
  ASSERT_EQ(message_node5->get_dependency_by_type(DependencyType(kMessageNode, kTidNode)),
            tid_node1);
  ASSERT_EQ(message_node5->get_dependency_by_type(DependencyType(kMessageNode, kMessageInputNode)),
            message_node2);
  ASSERT_EQ(message_node5->get_dependency_by_type(DependencyType(kMessageNode, kHandleNode + 0)),
            handle_nodea1);
  ASSERT_EQ(message_node5->get_dependency_by_type(DependencyType(kMessageNode, kHandleNode + 1)),
            handle_nodea2);
}

TEST(Comparator, UniqueMatchToGolden) {
  std::string messages = R"(
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)

echo_client 2:21 zx_channel_write(handle:handle: 221a, options:uint32: 0)

echo_client 1:12 zx_channel_write(handle:handle: 112a, options:uint32: 0)

echo_client 2:21   -> ZX_OK (out0:handle: 221b, out1:handle: 221c)
)";
  TestComparator comparator(messages);

  std::string message1 = "zx_channel_create(options:uint32: 0)\n";
  ActualMessageGraph message_graph1;
  auto message_node1 = message_graph1.InsertMessage("echo_client", 1, 1, message1);
  ASSERT_TRUE(comparator.UniqueMatchToGoldenTest(message_node1));

  std::string message2 = "zx_channel_write(handle:handle: 6, options:uint32: 0)\n";
  ActualMessageGraph message_graph2;
  auto message_node2 = message_graph2.InsertMessage("echo_client", 1, 1, message2);
  ASSERT_FALSE(comparator.UniqueMatchToGoldenTest(message_node2));

  std::string message3 = "zx_channel_read(handle:handle: 6, options:uint32: 0)\n";
  ActualMessageGraph message_graph3;
  auto message_node3 = message_graph3.InsertMessage("echo_client", 1, 1, message3);
  ASSERT_FALSE(comparator.UniqueMatchToGoldenTest(message_node3));
}

TEST(Comparator, PropagateMatch) {
  // A propagation that should work
  std::string golden_message1 = "echo_client 1:11 zx_channel_create(options:uint32: 0)\n";
  TestComparator comparator1(golden_message1);

  std::string message1 = "zx_channel_create(options:uint32: 0)\n";
  ActualMessageGraph message_graph1;
  auto message_node1 = message_graph1.InsertMessage("echo_client", 2, 22, message1);
  ASSERT_TRUE(comparator1.UniqueMatchToGoldenTest(
      message_node1));  // set the matching node for message_node1
  ASSERT_TRUE(comparator1.PropagateMatchTest(message_node1));
  auto golden_tid = comparator1.golden_message_graph().get_tid_node(11);
  auto actual_tid = message_graph1.get_tid_node(22);
  ASSERT_EQ(actual_tid->matching_golden_node(), golden_tid);

  auto golden_pid = comparator1.golden_message_graph().get_pid_node(1);
  auto actual_pid = message_graph1.get_pid_node(2);
  ASSERT_EQ(actual_pid->matching_golden_node(), golden_pid);

  std::string golden_message2 = R"(
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)

echo_client 1:21 zx_channel_write(handle:handle: 221a, options:uint32: 0)
)";
  TestComparator comparator2(golden_message2);

  std::string message2a = "zx_channel_create(options:uint32: 0)\n";
  ActualMessageGraph message_graph2;
  auto message_node2a = message_graph2.InsertMessage("echo_client", 2, 22, message2a);
  ASSERT_TRUE(comparator2.UniqueMatchToGoldenTest(message_node2a));
  ASSERT_TRUE(comparator2.PropagateMatchTest(message_node2a));

  std::string message2b = "  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)\n";
  auto message_node2b =
      message_graph2.InsertMessage("echo_client", 2, 22, message2b, message_node2a);
  ASSERT_TRUE(comparator2.UniqueMatchToGoldenTest(message_node2b));
  ASSERT_TRUE(comparator2.PropagateMatchTest(message_node2b));

  std::string message2c = "zx_channel_write(handle:handle: 221a, options:uint32: 0)\n";
  auto message_node2c = message_graph2.InsertMessage("echo_client", 2, 22, message2c);
  ASSERT_TRUE(comparator2.UniqueMatchToGoldenTest(message_node2c));
  ASSERT_FALSE(comparator2.PropagateMatchTest(message_node2c));
}

TEST(Comparator, ReversePropagateMatch) {
  // A reverse propagation that should work
  std::string golden_message1 = "echo_client 1:11 zx_channel_create(options:uint32: 0)\n";
  TestComparator comparator1(golden_message1);

  std::string message1 = "zx_channel_create(options:uint32: 0)\n";
  ActualMessageGraph message_graph1;
  auto message_node1 = message_graph1.InsertMessage("echo_client", 2, 22, message1);
  auto actual_pid = message_graph1.get_pid_node(2);
  auto golden_pid = comparator1.golden_message_graph().get_pid_node(1);
  actual_pid->set_matching_golden_node(golden_pid);
  ASSERT_TRUE(comparator1.ReversePropagateMatchTest(actual_pid));

  auto golden_tid = comparator1.golden_message_graph().get_tid_node(11);
  auto actual_tid = message_graph1.get_tid_node(22);
  ASSERT_EQ(actual_tid->matching_golden_node(), golden_tid);
  ASSERT_TRUE(message_node1->matching_golden_node());

  std::string golden_message2 = R"(
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 111a, out1:handle: 111b)
)";
  TestComparator comparator2(golden_message2);

  std::string message2 = "zx_channel_create(options:uint32: 0)\n";
  ActualMessageGraph message_graph2;
  auto message_node2 = message_graph2.InsertMessage("echo_client", 2, 22, message2);
  actual_pid = message_graph2.get_pid_node(2);
  golden_pid = comparator2.golden_message_graph().get_pid_node(1);
  actual_pid->set_matching_golden_node(golden_pid);
  ASSERT_FALSE(comparator1.ReversePropagateMatchTest(actual_pid));
}

TEST(Comparator, CompareInputOutput) {
  std::string messages = R"(
echo_client 1:11 zx_channel_create(options:uint32: 0)
  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)

echo_client 1:12 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)
)";
  fidlcat::TestComparator comparator(messages);
  std::string process_name = "echo_client";

  std::string input_1 = "echo_client 2:21 zx_channel_create(options:uint32: 0)\n";
  comparator.CompareInput(input_1, process_name, 2, 21);
  ASSERT_EQ("", comparator.output_string());

  std::string output_1 = "  -> ZX_OK (out0:handle: 6d3e0273, out1:handle: 6c2e0347)\n";
  comparator.CompareOutput(output_1, process_name, 2, 21);
  ASSERT_EQ("", comparator.output_string());

  std::string input_2 =
      "echo_client 2:21 zx_channel_write(handle:handle: 6d1e0003, options:uint32: 0)\n";
  comparator.CompareInput(input_2, process_name, 2, 21);
  ASSERT_EQ(
      "Conflicting matches for  actual message node: zx_channel_write(handle:handle: 0, "
      "options:uint32: 0)\n "
      "matched to  golden message node: zx_channel_write(handle:handle: 0, options:uint32: "
      "0)\n \n. "
      "Actual has dependency to  actual tid node: 21  matched to  golden tid node: 11  whereas "
      "according to dependency from actual and its match it should have been  golden tid node: 12 "
      "\n",
      comparator.output_string());
}

}  // namespace fidlcat
