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
  reply.status = -25;
  attach_remote_api()->last_attach->cb(Err(), reply);

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 attach-job failed.\nError attaching, status = -25.", event.output.AsString());
}

TEST_F(VerbAttachJob, Good) {
  // Good job koid.
  constexpr uint64_t kGoodJobKoid1 = 7890u;
  console().ProcessInputLine("attach-job 7890");

  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid1, attach_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = 0;
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
  EXPECT_EQ("Created Filter 1 \"foo\" for job 2.", event.output.AsString());
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Filter 2 \"bar\" for job 2.", event.output.AsString());

  // Report job attachment a success.
  ASSERT_TRUE(attach_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid2, attach_remote_api()->last_attach->request.koid);
  reply.status = 0;
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
  EXPECT_EQ("Created Filter 3 \"baz\" for job 2.", event.output.AsString());

  // Validate the filters.
  console().ProcessInputLine("filter");
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "  # Pattern Job\n"
      "  1 foo       2\n"
      "  2 bar       2\n"
      "▶ 3 baz       2\n",
      event.output.AsString());
}

}  // namespace zxdb
