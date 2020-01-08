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
    filter_requests_.push_back(request);

    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::JobFilterReply()); });
  }

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    debug_ipc::AttachReply reply;
    reply.koid = request.koid;
    reply.name = "test";

    MessageLoop::Current()->PostTask(FROM_HERE,
                                     [cb = std::move(cb), reply]() mutable { cb(Err(), reply); });
  }

  const std::vector<debug_ipc::JobFilterRequest>& filter_requests() const {
    return filter_requests_;
  }

 private:
  std::vector<debug_ipc::JobFilterRequest> filter_requests_;

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

void PrintVectorDifference(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  std::stringstream ss;
  ss << std::endl;
  ss << "Vector 1: " << std::endl;
  for (auto& s : a) {
    ss << " - " << s << std::endl;
  }

  ss << "Vector 2: " << std::endl;
  for (auto& s : b) {
    ss << " - " << s << std::endl;
  }

  FXL_LOG(ERROR) << ss.str();
}

bool EqualVectors(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.size() != b.size()) {
    PrintVectorDifference(a, b);
    return false;
  }

  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i]) {
      PrintVectorDifference(a, b);
      return false;
    }
  }

  return true;
}

}  // namespace

TEST_F(FilterTest, SetFilters) {
  Filter* filter = session().system().CreateNewFilter();

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context = contexts[0];
  bool job_context_alive;
  Err ctx_err;

  // No attached job, there should be no request.
  ASSERT_EQ(sink().filter_requests().size(), 0u);

  constexpr uint64_t kJobKoid = 1234;
  context->Attach(kJobKoid,
                  [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                    ctx_err = err;
                    job_context_alive = !!ctx;
                  });
  MessageLoop::Current()->RunUntilNoTasks();
  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  // We attached, but there is no filter to send yet.
  ASSERT_EQ(sink().filter_requests().size(), 0u);

  filter->SetPattern("foo");
  MessageLoop::Current()->RunUntilNoTasks();

  // There should be a filter request.
  ASSERT_EQ(sink().filter_requests().size(), 1u);
  EXPECT_EQ(sink().filter_requests()[0].job_koid, kJobKoid);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[0].filters, {"foo"}));

  // Deleting the filter should clean up the filters.
  session().system().DeleteFilter(filter);
  MessageLoop::Current()->RunUntilNoTasks();

  // There should be a filter request.
  ASSERT_EQ(sink().filter_requests().size(), 2u);
  EXPECT_EQ(sink().filter_requests()[1].job_koid, kJobKoid);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[1].filters, {}));
}

TEST_F(FilterTest, SetSpecificFilters) {
  Filter* filter = session().system().CreateNewFilter();

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context_a = contexts[0];
  auto context_b = session().system().CreateNewJobContext();
  bool job_context_alive;
  Err ctx_err;

  constexpr uint64_t kJobKoid1 = 1234;
  context_a->Attach(kJobKoid1,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                      /* MessageLoop::Current()->QuitNow(); */
                    });
  MessageLoop::Current()->RunUntilNoTasks();
  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  constexpr uint64_t kJobKoid2 = 5678;
  context_b->Attach(kJobKoid2,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                    });
  MessageLoop::Current()->RunUntilNoTasks();
  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  // There should be no filter requests yet.
  ASSERT_EQ(sink().filter_requests().size(), 0u);

  filter->SetPattern("foo");
  MessageLoop::Current()->RunUntilNoTasks();

  // There should be two filter requests.
  ASSERT_EQ(sink().filter_requests().size(), 2u);
  EXPECT_EQ(sink().filter_requests()[0].job_koid, kJobKoid1);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[0].filters, {"foo"}));
  EXPECT_EQ(sink().filter_requests()[1].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[1].filters, {"foo"}));

  // Filtering to one should make one job should remove the second filter.
  filter->SetJob(context_a);
  MessageLoop::Current()->RunUntilNoTasks();
  ASSERT_EQ(sink().filter_requests().size(), 3u);
  EXPECT_EQ(sink().filter_requests()[2].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[2].filters, {}));

  // Moving it over to the second job should create two requests.
  filter->SetJob(context_b);
  MessageLoop::Current()->RunUntilNoTasks();
  ASSERT_EQ(sink().filter_requests().size(), 5u);
  EXPECT_EQ(sink().filter_requests()[3].job_koid, kJobKoid1);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[3].filters, {}));
  EXPECT_EQ(sink().filter_requests()[4].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[4].filters, {"foo"}));

  // Deleting the filter should cleanup the one left.
  session().system().DeleteFilter(filter);
  MessageLoop::Current()->RunUntilNoTasks();
  ASSERT_EQ(sink().filter_requests().size(), 6u);
  EXPECT_EQ(sink().filter_requests()[5].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[5].filters, {}));
}

TEST_F(FilterTest, SetExAnteFilters) {
  Filter* filter = session().system().CreateNewFilter();
  filter->SetPattern("foo");

  // There should be no filter requests yet.
  ASSERT_EQ(sink().filter_requests().size(), 0u);

  auto contexts = session().system().GetJobContexts();
  ASSERT_EQ(1u, contexts.size());
  auto context_a = contexts[0];
  auto context_b = session().system().CreateNewJobContext();
  bool job_context_alive;
  Err ctx_err;

  // There should be no filter requests yet.
  ASSERT_EQ(sink().filter_requests().size(), 0u);

  constexpr uint64_t kJobKoid1 = 1234;
  context_a->Attach(kJobKoid1,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                    });
  MessageLoop::Current()->RunUntilNoTasks();
  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  ASSERT_EQ(sink().filter_requests().size(), 1u);
  EXPECT_EQ(sink().filter_requests()[0].job_koid, kJobKoid1);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[0].filters, {"foo"}));

  constexpr uint64_t kJobKoid2 = 5678;
  context_b->Attach(kJobKoid2,
                    [&job_context_alive, &ctx_err](fxl::WeakPtr<JobContext> ctx, const Err& err) {
                      ctx_err = err;
                      job_context_alive = !!ctx;
                    });
  MessageLoop::Current()->RunUntilNoTasks();
  EXPECT_FALSE(ctx_err.has_error());
  EXPECT_TRUE(job_context_alive);

  // The second filter message should go out.
  ASSERT_EQ(sink().filter_requests().size(), 2u);
  EXPECT_EQ(sink().filter_requests()[1].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[1].filters, {"foo"}));

  // Adding a second filter only the second job should notifiy only that one.
  Filter* filter2 = session().system().CreateNewFilter();
  filter2->SetJob(context_b);
  MessageLoop::Current()->RunUntilNoTasks();

  // Still no new message.
  ASSERT_EQ(sink().filter_requests().size(), 2u);
  filter2->SetPattern("bar");
  MessageLoop::Current()->RunUntilNoTasks();

  // Should only be a message for the second job.
  ASSERT_EQ(sink().filter_requests().size(), 3u);
  EXPECT_EQ(sink().filter_requests()[2].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[2].filters, {"foo", "bar"}));

  // Deleting the first job should affect both jobs.
  session().system().DeleteFilter(filter);
  MessageLoop::Current()->RunUntilNoTasks();

  // A delete for both jobs.
  ASSERT_EQ(sink().filter_requests().size(), 5u);
  EXPECT_EQ(sink().filter_requests()[3].job_koid, kJobKoid1);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[3].filters, {}));
  EXPECT_EQ(sink().filter_requests()[4].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[4].filters, {"bar"}));

  // Deleting the last one should only affect the last job.
  session().system().DeleteFilter(filter2);
  MessageLoop::Current()->RunUntilNoTasks();

  ASSERT_EQ(sink().filter_requests().size(), 6u);
  EXPECT_EQ(sink().filter_requests()[5].job_koid, kJobKoid2);
  EXPECT_TRUE(EqualVectors(sink().filter_requests()[5].filters, {}));
}

}  // namespace zxdb
