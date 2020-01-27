// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_MESSAGES_GRAPH_TEST_H_
#define TOOLS_FIDLCAT_LIB_MESSAGES_GRAPH_TEST_H_

#include "tools/fidlcat/lib/message_graph.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

class GoldenMessageGraphTest : public GoldenMessageGraph {
 public:
  static std::map<uint32_t, int> ReplacesHandlesWithTokensTest(std::string* message) {
    return ReplacesHandlesWithTokens(message);
  }
}

namespace fidlcat {
  TEST(MessageGraph, GoldenGraphConstruction) {
    std::string message100 = "100";
    std::string message101 = "101";
    std::string message110 = "110";
    std::string message200 = "200";
    std::string process1 = "process1";
    std::string process2 = "process2";
    std::string pid = "pid";
    std::string tid = "tid";
    std::string message = "message";
    GoldenMessageGraph messages_graph;

    auto message_node100 = messages_graph.InsertMessage(process1, 1, 10, message100);
    auto message_node101 = messages_graph.InsertMessage(process1, 1, 10, message101);
    auto message_node110 = messages_graph.InsertMessage(process1, 1, 11, message110);
    auto message_node200 = messages_graph.InsertMessage(process2, 2, 20, message200);

    // Pid nodes are present.
    auto pid1 = messages_graph.get_pid_node(1);
    auto pid2 = messages_graph.get_pid_node(2);

    // Tid nodes and their link to pid nodes as well.
    // We use here that pid is the first dependency added to a tid.
    auto tid10 = messages_graph.get_tid_node(10);
    constexpr DependencyType kTidPidLink = DependencyType(kTidNode, kPidNode);
    constexpr DependencyType kMessageTidLink = DependencyType(kMessageNode, kTidNode);
    auto pid_of_tid10 = tid10->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid10, pid1);

    auto tid11 = messages_graph.get_tid_node(11);
    auto pid_of_tid11 = tid11->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid11, pid1);

    auto tid20 = messages_graph.get_tid_node(20);
    auto pid_of_tid20 = tid20->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid20, pid2);

    // And the reverse links from pid nodes to tid nodes.
    auto links_pid1 = pid1->get_reverse_dependencies_by_type(kTidPidLink);
    ASSERT_EQ(links_pid1->second.size(), 2u);
    ASSERT_TRUE(links_pid1->second[0].lock() == tid10 || links_pid1->second[0].lock() == tid11);
    ASSERT_TRUE(links_pid1->second[1].lock() == tid10 || links_pid1->second[1].lock() == tid11);

    auto links_pid2 = pid2->get_reverse_dependencies_by_type(kTidPidLink);
    ASSERT_EQ(links_pid2->second.size(), 1u);
    ASSERT_EQ(links_pid2->second[0].lock(), tid20);

    // Message nodes and their links to tid nodes.
    // We use here that tid is the first dependency added to a message.
    ASSERT_EQ(tid10, message_node100->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid10, message_node101->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid11, message_node110->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid20, message_node200->get_dependency_by_type(kMessageTidLink));

    // And the reverse links from tid nodes to messages.
    auto links_tid10 = tid10->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid10->second.size(), 2u);
    ASSERT_TRUE(links_tid10->second[0].lock() == message_node100 ||
                links_tid10->second[0].lock() == message_node101);
    ASSERT_TRUE(links_tid10->second[1].lock() == message_node100 ||
                links_tid10->second[1].lock() == message_node101);

    auto links_tid11 = tid11->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid11->second.size(), 1u);
    ASSERT_EQ(links_tid11->second[0].lock(), message_node110);

    auto links_tid20 = tid20->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid20->second.size(), 1u);
    ASSERT_EQ(links_tid20->second[0].lock(), message_node200);
  }

  TEST(MessageGraph, ActualGraphConstruction) {
    std::string message100 = "100";
    std::string message101 = "101";
    std::string message110 = "110";
    std::string message200 = "200";
    std::string process1 = "process1";
    std::string process2 = "process2";
    std::string pid = "pid";
    std::string tid = "tid";
    std::string message = "message";
    ActualMessageGraph messages_graph;

    auto message_node100 = messages_graph.InsertMessage(process1, 1, 10, message100);
    auto message_node101 = messages_graph.InsertMessage(process1, 1, 10, message101);
    auto message_node110 = messages_graph.InsertMessage(process1, 1, 11, message110);
    auto message_node200 = messages_graph.InsertMessage(process2, 2, 20, message200);

    // Pid nodes are present.
    auto pid1 = messages_graph.get_pid_node(1);
    auto pid2 = messages_graph.get_pid_node(2);

    constexpr DependencyType kTidPidLink = DependencyType(kTidNode, kPidNode);
    constexpr DependencyType kMessageTidLink = DependencyType(kMessageNode, kTidNode);

    // Tid nodes and their link to pid nodes as well.
    // We use here that pid is the first dependency added to a tid.
    auto tid10 = messages_graph.get_tid_node(10);
    auto pid_of_tid10 = tid10->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid10, pid1);

    auto tid11 = messages_graph.get_tid_node(11);
    auto pid_of_tid11 = tid11->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid11, pid1);

    auto tid20 = messages_graph.get_tid_node(20);
    auto pid_of_tid20 = tid20->get_dependency_by_type(kTidPidLink);
    ASSERT_EQ(pid_of_tid20, pid2);

    // And the reverse links from pid nodes to tid nodes.
    auto links_pid1 = pid1->get_reverse_dependencies_by_type(kTidPidLink);
    ASSERT_EQ(links_pid1->second.size(), 2u);
    ASSERT_TRUE(links_pid1->second[0].lock() == tid10 || links_pid1->second[0].lock() == tid11);
    ASSERT_TRUE(links_pid1->second[1].lock() == tid10 || links_pid1->second[1].lock() == tid11);

    auto links_pid2 = pid2->get_reverse_dependencies_by_type(kTidPidLink);
    ASSERT_EQ(links_pid2->second.size(), 1u);
    ASSERT_EQ(links_pid2->second[0].lock(), tid20);

    // Message nodes and their links to tid nodes.
    // We use here that tid is the first dependency added to a message.
    ASSERT_EQ(tid10, message_node100->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid10, message_node101->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid11, message_node110->get_dependency_by_type(kMessageTidLink));
    ASSERT_EQ(tid20, message_node200->get_dependency_by_type(kMessageTidLink));

    // And the reverse links from tid nodes to messages.
    auto links_tid10 = tid10->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid10->second.size(), 2u);
    ASSERT_TRUE(links_tid10->second[0].lock() == message_node100 ||
                links_tid10->second[0].lock() == message_node101);
    ASSERT_TRUE(links_tid10->second[1].lock() == message_node100 ||
                links_tid10->second[1].lock() == message_node101);

    auto links_tid11 = tid11->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid11->second.size(), 1u);
    ASSERT_EQ(links_tid11->second[0].lock(), message_node110);

    auto links_tid20 = tid20->get_reverse_dependencies_by_type(kMessageTidLink);
    ASSERT_EQ(links_tid20->second.size(), 1u);
    ASSERT_EQ(links_tid20->second[0].lock(), message_node200);
  }

  TEST(MessageGraph, ReplacesHandlesWithTokens) {
    std::string message = "handle: 1, out handle = 4, in handle = 1\n";
    std::vector<uint32_t> handle_tokens =
        GoldenMessageGraphTest::ReplacesHandlesWithTokensTest(&message);
    ASSERT_EQ(message, "handle: 0, out handle = 1, in handle = 2\n");
    ASSERT_EQ(handle_tokens[0], 1);
    ASSERT_EQ(handle_tokens[1], 4);
    ASSERT_EQ(handle_tokens[2], 1);
  }

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_MESSAGES_GRAPH_TEST_H_
