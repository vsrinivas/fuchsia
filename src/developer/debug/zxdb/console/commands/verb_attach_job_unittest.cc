// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach_job.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class JobTestRemoteApi : public RemoteAPI {
 public:
  struct AttachLog {
    debug_ipc::AttachRequest request;
    fit::callback<void(const Err&, debug_ipc::AttachReply)> cb;
  };
  struct JobFilterLog {
    debug_ipc::JobFilterRequest request;
    fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb;
  };

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    last_attach = AttachLog{request, std::move(cb)};
  }

  void JobFilter(const debug_ipc::JobFilterRequest& request,
                 fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) override {
    filters.push_back(JobFilterLog{request, std::move(cb)});
  }

  // Stores the last one.
  std::optional<AttachLog> last_attach;

  // Stores a log of all requests (since the tests needs all of them).
  std::vector<JobFilterLog> filters;
};

class VerbAttachJob : public ConsoleTest {
 public:
  JobTestRemoteApi* job_remote_api() { return job_remote_api_; }

 protected:
  // RemoteAPITest overrides.
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<JobTestRemoteApi>();
    job_remote_api_ = remote_api.get();
    return remote_api;
  }

  JobTestRemoteApi* job_remote_api_ = nullptr;
};

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
  ASSERT_TRUE(job_remote_api()->last_attach);
  ASSERT_EQ(kBadJobKoid, job_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = -25;
  job_remote_api()->last_attach->cb(Err(), reply);

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 attach-job failed.\nError attaching, status = -25.", event.output.AsString());
}

TEST_F(VerbAttachJob, Good) {
  // Good job koid.
  constexpr uint64_t kGoodJobKoid1 = 7890u;
  console().ProcessInputLine("attach-job 7890");

  ASSERT_TRUE(job_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid1, job_remote_api()->last_attach->request.koid);
  debug_ipc::AttachReply reply;
  reply.status = 0;
  reply.koid = kGoodJobKoid1;
  reply.name = "some job";
  job_remote_api()->last_attach->cb(Err(), reply);
  EXPECT_TRUE(job_remote_api()->filters.empty());

  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 1 [Attached] koid=7890 some job", event.output.AsString());

  // Attaching a job with some filters. Since the current job is already attached, this should make
  // a new job (#2).
  constexpr uint64_t kGoodJobKoid2 = 5555u;
  job_remote_api()->last_attach = std::nullopt;
  console().ProcessInputLine("attach-job 5555 foo bar");

  // It will immediately make two filters to apply to the new job.
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created filter 1 on job 2 for processes containing \"foo\".\n",
            event.output.AsString());
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created filter 2 on job 2 for processes containing \"bar\".\n",
            event.output.AsString());

  // Report job attachment a success.
  ASSERT_TRUE(job_remote_api()->last_attach);
  ASSERT_EQ(kGoodJobKoid2, job_remote_api()->last_attach->request.koid);
  reply.status = 0;
  reply.koid = kGoodJobKoid2;
  reply.name = "other job";
  job_remote_api()->last_attach->cb(Err(), reply);

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Job 2 [Attached] koid=5555 other job", event.output.AsString());

  // The single update should contain both filters since it should be sent after the job attach
  // callback is run (so both filters will be applied at once even though they were created in
  // sequence).
  ASSERT_EQ(1u, job_remote_api()->filters.size());
  auto& filters0 = job_remote_api()->filters[0].request;
  EXPECT_EQ(kGoodJobKoid2, filters0.job_koid);
  ASSERT_EQ(2u, filters0.filters.size());
  EXPECT_EQ("foo", filters0.filters[0]);
  EXPECT_EQ("bar", filters0.filters[1]);

  // Attach to the same koid. This should append the filter.
  job_remote_api()->last_attach = std::nullopt;
  console().ProcessInputLine("attach-job 5555 baz");
  ASSERT_FALSE(job_remote_api()->last_attach);  // No messages should have been sent.

  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created filter 3 on job 2 for processes containing \"baz\".\n",
            event.output.AsString());

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
