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
  EXPECT_EQ("Not enough arguments when reading the job koid.", event.output.AsString());

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
  EXPECT_TRUE(attach_remote_api()->filters.empty());

  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 state=Attached koid=7890 name=\"some job\"", event.output.AsString());

  // Attaching a job with some filters. Since the current job is already attached, this should make
  // a new job (#2).
  constexpr uint64_t kGoodJobKoid2 = 5555u;
  attach_remote_api()->last_attach = std::nullopt;
  console().ProcessInputLine("attach-job 5555 foo bar");

  // It will immediately make two filters to apply to the new job.
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Filter 1 pattern=foo job=2", event.output.AsString());
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Filter 2 pattern=bar job=2", event.output.AsString());

  // Report job attachment a success.
  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid2, attach_remote_api()->last_attach->request.koid);
  reply.status = debug::Status();
  reply.koid = kGoodJobKoid2;
  reply.name = "other job";
  attach_remote_api()->last_attach->cb(Err(), reply);

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 2 state=Attached koid=5555 name=\"other job\"", event.output.AsString());

  // The single update should contain both filters since it should be sent after the job attach
  // callback is run (so both filters will be applied at once even though they were created in
  // sequence).
  ASSERT_EQ(1u, attach_remote_api()->filters.size());
  auto& filters0 = attach_remote_api()->filters[0].request;
  EXPECT_EQ(kGoodJobKoid2, filters0.job_koid);
  ASSERT_EQ(2u, filters0.filters.size());
  EXPECT_EQ("foo", filters0.filters[0]);
  EXPECT_EQ("bar", filters0.filters[1]);

  // Attach to the same koid. This should append the filter.
  attach_remote_api()->last_attach = std::nullopt;
  console().ProcessInputLine("attach-job 5555 baz");
  ASSERT_FALSE(attach_remote_api()->last_attach);  // No messages should have been sent.

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Filter 3 pattern=baz job=2", event.output.AsString());

  // Validate the jobs and filters.
  console().ProcessInputLine("job");
  event = console().GetOutputEvent();
  EXPECT_EQ(
      "  # State    Koid Name\n"
      "  1 Attached 7890 some job\n"
      "▶ 2 Attached 5555 other job\n",
      event.output.AsString());

  console().ProcessInputLine("filter");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "  # pattern job\n"
      "  1 foo       2\n"
      "  2 bar       2\n"
      "▶ 3 baz       2\n",
      event.output.AsString());

  // Remove the active job. This should delete the associated filters also.
  console().ProcessInputLine("job 2 rm");
  event = console().GetOutputEvent();
  EXPECT_EQ(R"(Removed Job 2 state=Attached koid=5555 name="other job")", event.output.AsString());

  console().ProcessInputLine("job");
  event = console().GetOutputEvent();
  EXPECT_EQ(
      " # State    Koid Name\n"
      " 1 Attached 7890 some job\n",
      event.output.AsString());

  console().ProcessInputLine("filter");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("No filters.\n", event.output.AsString());

  // Attaching again should maje a new job object and set it as the default.
  console().ProcessInputLine("attach-job 5555");
  attach_remote_api()->last_attach->cb(Err(), reply);
  event = console().GetOutputEvent();
  EXPECT_EQ("Job 3 state=Attached koid=5555 name=\"other job\"", event.output.AsString());

  console().ProcessInputLine("job");
  event = console().GetOutputEvent();
  EXPECT_EQ(
      "  # State    Koid Name\n"
      "  1 Attached 7890 some job\n"
      "▶ 3 Attached 5555 other job\n",
      event.output.AsString());
}

}  // namespace zxdb
