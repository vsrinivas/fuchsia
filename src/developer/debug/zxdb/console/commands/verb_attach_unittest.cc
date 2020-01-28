// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/commands/attach_command_test.h"

namespace zxdb {

namespace {

class VerbAttach : public AttachCommandTest {};

}  // namespace

TEST_F(VerbAttach, Bad) {
  // Missing argument.
  console().ProcessInputLine("attach");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Wrong number of arguments to attach.", event.output.AsString());

  // Can't attach to a process by filter.
  console().ProcessInputLine("process attach foo");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Attaching by process name (a non-numeric argument)\nonly supports the \"job\" noun.",
            event.output.AsString());
}

TEST_F(VerbAttach, Koid) {
  constexpr uint64_t kKoid = 7890u;
  console().ProcessInputLine("attach 7890");

  // This should create a new process context and give "process 2" because the default console test
  // harness makes a mock runnin process by default.
  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kKoid, attach_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = 0;
  reply.koid = kKoid;
  reply.name = "some process";
  attach_remote_api()->last_attach->cb(Err(), reply);
  EXPECT_TRUE(attach_remote_api()->filters.empty());

  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Process 2 state=Running koid=7890 name=\"some process\"", event.output.AsString());
}

TEST_F(VerbAttach, Filter) {
  // Normal filter case.
  console().ProcessInputLine("attach foo");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Waiting for process matching \"foo\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());

  // Don't allow attaching by wildcard without a job.
  console().ProcessInputLine("attach *");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Use a specific job (\"job 3 attach *\") when attaching to all processes.",
            event.output.AsString());

  // Wildcard within a job is OK.
  console().ProcessInputLine("job 1 attach *");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Waiting for process matching \"*\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());
}

}  // namespace zxdb
