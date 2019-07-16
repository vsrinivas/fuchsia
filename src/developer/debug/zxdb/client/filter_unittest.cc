// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class FilterSink : public RemoteAPI {
 public:
  void JobFilter(const debug_ipc::JobFilterRequest& request,
                 fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) override {
    filters[request.job_koid] = request.filters;

    MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), debug_ipc::JobFilterReply());
      MessageLoop::Current()->QuitNow();
    });
  }

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    debug_ipc::AttachReply reply;
    reply.koid = request.koid;
    reply.name = "test";
    MessageLoop::Current()->PostTask(FROM_HERE,
                                     [cb = std::move(cb), reply]() mutable { cb(Err(), reply); });
  }

  std::map<uint64_t, std::vector<std::string>> filters;
};

class FilterTest : public RemoteAPITest {
 public:
  FilterTest() = default;
  ~FilterTest() override = default;

  FilterSink& sink() { return *sink_; }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<FilterSink>();
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  FilterSink* sink_;  // Owned by the session.
};

}  // namespace

TEST_F(FilterTest, SetFilters) {
  Filter* filter = session().system().CreateNewFilter();

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context = contexts[0];
  bool job_context_alive;
  Err ctx_err;

  context->Attach(1234,
                  [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                    ctx_err = err;
                    job_context_alive = !!ctx;
                    MessageLoop::Current()->QuitNow();
                  });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  filter->SetPattern("foo");
  MessageLoop::Current()->Run();

  ASSERT_EQ(1u, sink().filters[1234u].size());
  EXPECT_EQ("foo", sink().filters[1234u][0]);
}

TEST_F(FilterTest, SetSpecificFilters) {
  Filter* filter = session().system().CreateNewFilter();

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context_a = contexts[0];
  auto context_b = session().system().CreateNewJobContext(context_a);
  bool job_context_alive;
  Err ctx_err;

  context_a->Attach(1234,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  context_b->Attach(5678,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  filter->SetPattern("foo");
  // Two jobs update, two quits.
  MessageLoop::Current()->Run();
  MessageLoop::Current()->Run();
  filter->SetJob(context_a);
  MessageLoop::Current()->Run();

  ASSERT_EQ(1u, sink().filters[1234u].size());
  EXPECT_EQ("foo", sink().filters[1234u][0]);
  ASSERT_EQ(0u, sink().filters[5678u].size());
}

TEST_F(FilterTest, SetBroadFilters) {
  Filter* filter = session().system().CreateNewFilter();

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context_a = contexts[0];
  auto context_b = session().system().CreateNewJobContext(context_a);
  bool job_context_alive;
  Err ctx_err;

  context_a->Attach(1234,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  context_b->Attach(5678,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  filter->SetPattern("foo");
  MessageLoop::Current()->Run();

  ASSERT_EQ(1u, sink().filters[1234u].size());
  EXPECT_EQ("foo", sink().filters[1234u][0]);
  ASSERT_EQ(1u, sink().filters[5678u].size());
  EXPECT_EQ("foo", sink().filters[5678u][0]);
}

TEST_F(FilterTest, SetExAnteFilters) {
  Filter* filter = session().system().CreateNewFilter();

  filter->SetPattern("foo");

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context_a = contexts[0];
  auto context_b = session().system().CreateNewJobContext(context_a);
  bool job_context_alive;
  Err ctx_err;

  context_a->Attach(1234,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  context_b->Attach(5678,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      MessageLoop::Current()->QuitNow();
                    });
  MessageLoop::Current()->Run();
  // One spin to attach the job, another spin to update the filters.
  MessageLoop::Current()->Run();

  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  ASSERT_EQ(1u, sink().filters[1234u].size());
  EXPECT_EQ("foo", sink().filters[1234u][0]);

  ASSERT_EQ(1u, sink().filters[5678u].size());
  EXPECT_EQ("foo", sink().filters[5678u][0]);
}

}  // namespace zxdb
