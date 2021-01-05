// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_ps.h"

#include <gtest/gtest.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

debug_ipc::ProcessTreeReply GetCannedProcessTreeReply() {
  debug_ipc::ProcessTreeReply reply;
  reply.root.koid = 1;
  reply.root.name = "root";

  // Make two nested jobs with a process in it.

  reply.root.children.emplace_back();
  reply.root.children[0].koid = 100;
  reply.root.children[0].name = "j1";

  reply.root.children[0].children.emplace_back();
  reply.root.children[0].children[0].koid = 101;
  reply.root.children[0].children[0].name = "j2";

  reply.root.children[0].children[0].children.emplace_back();
  reply.root.children[0].children[0].children[0].type =
      debug_ipc::ProcessTreeRecord::Type::kProcess;
  reply.root.children[0].children[0].children[0].koid = 102;
  reply.root.children[0].children[0].children[0].name = "baz";

  // And add a process at the top level.
  reply.root.children.emplace_back();
  reply.root.children[1].type = debug_ipc::ProcessTreeRecord::Type::kProcess;
  reply.root.children[1].koid = 200;
  reply.root.children[1].name = "foo bar";
  return reply;
}

// Implements MockRemoteAPI to return a process tree.
class PSTestRemoteAPI : public MockRemoteAPI {
 public:
  void ProcessTree(const debug_ipc::ProcessTreeRequest& request,
                   fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)> cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), GetCannedProcessTreeReply());

      // Give control back to the test.
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  }
};

class VerbPSTest : public ConsoleTest {
 public:
  std::string RunCommandAndGetOutput(const std::string& cmd) {
    console().ProcessInputLine(cmd);
    loop().Run();

    auto event = console().GetOutputEvent();
    EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
    return event.output.AsString();
  }

 protected:
  virtual std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    return std::make_unique<PSTestRemoteAPI>();
  }
};

}  // namespace

TEST_F(VerbPSTest, Filter) {
  // "ps" by itself should show everything.
  EXPECT_EQ(RunCommandAndGetOutput("ps b"),
            "j: 1 root\n"
            "  j: 100 j1\n"
            "    j: 101 j2\n"
            "      p: 102 baz\n"
            "  p: 200 foo bar\n");

  // Both processes have a "b" in them, this should match everything.
  EXPECT_EQ(RunCommandAndGetOutput("ps b"),
            "j: 1 root\n"
            "  j: 100 j1\n"
            "    j: 101 j2\n"
            "      p: 102 baz\n"
            "  p: 200 foo bar\n");

  // Look for just a job name.
  EXPECT_EQ(RunCommandAndGetOutput("ps j2"),
            "j: 1 root\n"
            "  j: 100 j1\n"
            "    j: 101 j2\n");

  // Look for just one process name with a space in it (matches "foo bar").
  EXPECT_EQ(RunCommandAndGetOutput("ps o b"),
            "j: 1 root\n"
            "  p: 200 foo bar\n");

  // Matches nothing.
  EXPECT_EQ(RunCommandAndGetOutput("ps zzz"), "No processes or jobs matching \"zzz\".\n");
}

}  // namespace zxdb
