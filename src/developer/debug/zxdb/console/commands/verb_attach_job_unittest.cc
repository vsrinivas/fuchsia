// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach_job.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/commands/attach_command_test.h"

namespace zxdb {

namespace {

class VerbAttachJob : public AttachCommandTest {};

}  // namespace

TEST_F(VerbAttachJob, Bad) {
  // No arguments.
  console().ProcessInputLine("attach-job");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Invalid number of arguments.", event.output.AsString());

  // Too many arguments.
  console().ProcessInputLine("attach-job 12345 foo");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Invalid number of arguments.", event.output.AsString());

  // Bad job koid.
  constexpr uint64_t kBadJobKoid = 12345u;
  console().ProcessInputLine("attach-job 12345");
  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kBadJobKoid, attach_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = debug::Status("Bad job");
  attach_remote_api()->last_attach->cb(Err(), reply);

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 attach-job failed.\nError attaching: Bad job", event.output.AsString());

  // Remove the job, this will delete the default job that exists.
  console().ProcessInputLine("job 1 rm");
  event = console().GetOutputEvent();
  EXPECT_EQ(R"(Removed Job 1 state="Not attached" name="")", event.output.AsString());

  // Now try to attach again (with no job objects alive), this should create a new job object but
  // fail to attach it.
  console().ProcessInputLine("attach-job 12345");
  attach_remote_api()->last_attach->cb(Err(), reply);
  event = console().GetOutputEvent();
  EXPECT_EQ("Job 2 attach-job failed.\nError attaching: Bad job", event.output.AsString());
}

TEST_F(VerbAttachJob, Good) {
  // Good job koid.
  constexpr uint64_t kGoodJobKoid1 = 7890u;
  console().ProcessInputLine("attach-job 7890");

  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid1, attach_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = debug::Status();
  reply.koid = kGoodJobKoid1;
  reply.name = "some job";
  attach_remote_api()->last_attach->cb(Err(), reply);

  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 state=Attached koid=7890 name=\"some job\"", event.output.AsString());
}

}  // namespace zxdb
