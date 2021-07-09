// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job.h"

#include <gtest/gtest.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

namespace {

class JobSink : public RemoteAPI {
 public:
  void JobFilter(const debug_ipc::JobFilterRequest& request,
                 fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) override {
    requests_.push_back(request);

    // Respond according to the configuration.
    if (err_.has_error()) {
      cb(err_, {});
      return;
    }

    debug_ipc::JobFilterReply reply;
    reply.status = status_;
    reply.matched_processes = pids_;
    cb(Err(), reply);
  }

  // Any attachment is a no-op.
  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {}

  void set_status(debug::Status status) { status_ = std::move(status); };
  void set_err(Err err) { err_ = err; }
  void set_pids(std::vector<uint64_t> pids) { pids_ = std::move(pids); }

  const std::vector<debug_ipc::JobFilterRequest>& requests() const { return requests_; }

  debug::Status status_;
  Err err_;
  std::vector<uint64_t> pids_;

  std::vector<debug_ipc::JobFilterRequest> requests_;
};

class JobTest : public RemoteAPITest {
 public:
  JobTest() = default;
  ~JobTest() override = default;

  JobSink* sink() { return sink_; }

 private:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<JobSink>();
    sink_ = sink.get();
    return sink;
  }

 private:
  JobSink* sink_;  // Owned by the session.
};

class MockFilterObserver : public FilterObserver {
 public:
  struct FilterMatchResult {
    Job* job;
    std::vector<uint64_t> matched_pids;
  };

  void OnFilterMatches(Job* job, const std::vector<uint64_t>& matched_pids) override {
    filter_matches_.push_back({job, matched_pids});
  }

  const std::vector<FilterMatchResult>& filter_matches() const { return filter_matches_; }

 private:
  std::vector<FilterMatchResult> filter_matches_;
};

// Helpers -----------------------------------------------------------------------------------------

template <typename T>
std::string PrintError(const std::vector<T>& from, const std::vector<T>& to) {
  std::stringstream ss;

  ss << "From vector: ";
  for (auto& i : from) {
    ss << i << ", ";
  }
  ss << std::endl;

  ss << "To vector: ";
  for (auto& i : to) {
    ss << i << ", ";
  }
  ss << std::endl;

  return ss.str();
}

template <typename T>
void CompareVectors(std::vector<T> from, std::vector<T> to) {
  if (from.size() != to.size())
    FAIL() << PrintError(to, from);

  std::sort(from.begin(), from.end());
  std::sort(to.begin(), to.end());

  for (size_t i = 0; i < from.size(); i++) {
    EXPECT_EQ(from[i], to[i]) << "Index " << i << "." << std::endl << PrintError(from, to);
  }
}

// Tests -------------------------------------------------------------------------------------------

TEST_F(JobTest, ErrShouldNotSignal) {
  MockFilterObserver observer;
  session().AddFilterObserver(&observer);

  // Set an err.
  constexpr char kError[] = "some error";
  sink()->set_err(Err(kError));

  constexpr uint64_t kJobKoid = 0x1234;
  Job job(&session(), false);
  job.AttachForTesting(kJobKoid, "job-name");

  // There should be no initial requests.
  ASSERT_TRUE(sink()->requests().empty());

  std::vector<std::string> filters = {"some", "filters"};
  job.SendAndUpdateFilters(filters);

  ASSERT_EQ(sink()->requests().size(), 1u);
  auto& request = sink()->requests().back();
  EXPECT_EQ(request.job_koid, kJobKoid);

  CompareVectors(request.filters, filters);

  // There should be no match signal.
  ASSERT_TRUE(observer.filter_matches().empty());
}

TEST_F(JobTest, NoZX_OKShouldNotSignal) {
  MockFilterObserver observer;
  session().AddFilterObserver(&observer);

  sink()->set_status(debug::Status("Invalid args"));

  constexpr uint64_t kJobKoid = 0x1234;
  Job job(&session(), false);
  job.AttachForTesting(kJobKoid, "job-name");

  // There should be no initial requests.
  ASSERT_TRUE(sink()->requests().empty());

  std::vector<std::string> filters = {"some", "filters"};
  job.SendAndUpdateFilters(filters);

  ASSERT_EQ(sink()->requests().size(), 1u);
  auto& request = sink()->requests().back();
  EXPECT_EQ(request.job_koid, kJobKoid);

  CompareVectors(request.filters, filters);

  // There should be no match signal.
  ASSERT_TRUE(observer.filter_matches().empty());
}

TEST_F(JobTest, OkResponseShouldSignal) {
  MockFilterObserver observer;
  session().AddFilterObserver(&observer);

  std::vector<uint64_t> pids = {1, 2, 3, 4};
  sink()->set_pids(pids);

  constexpr uint64_t kJobKoid = 0x1234;
  Job job(&session(), false);
  job.AttachForTesting(kJobKoid, "job-name");

  // There should be no initial requests.
  ASSERT_TRUE(sink()->requests().empty());

  std::vector<std::string> filters = {"some", "filters"};
  job.SendAndUpdateFilters(filters);

  {
    ASSERT_EQ(sink()->requests().size(), 1u);
    auto& request = sink()->requests().back();
    EXPECT_EQ(request.job_koid, kJobKoid);
    CompareVectors(request.filters, filters);

    // There should be no match addiotional signal.
    ASSERT_EQ(observer.filter_matches().size(), 1u);
    auto& filter_match = observer.filter_matches().back();
    EXPECT_EQ(filter_match.job, &job);
    CompareVectors(filter_match.matched_pids, pids);
  }

  // Setting same filters should not send.
  sink()->set_pids({1, 2});
  job.SendAndUpdateFilters(filters);

  ASSERT_EQ(sink()->requests().size(), 1u);
  ASSERT_EQ(observer.filter_matches().size(), 1u);

  job.SendAndUpdateFilters({"some"});
  {
    // Sending less filters should send a request.
    ASSERT_EQ(sink()->requests().size(), 2u);
    auto& request = sink()->requests().back();
    EXPECT_EQ(request.job_koid, kJobKoid);
    CompareVectors(request.filters, {"some"});

    // There should be a match requests.
    ASSERT_EQ(observer.filter_matches().size(), 2u);
    auto& filter_match = observer.filter_matches().back();
    EXPECT_EQ(filter_match.job, &job);
    CompareVectors(filter_match.matched_pids, {1, 2});
  }
}

TEST_F(JobTest, MultipleJobs) {
  MockFilterObserver observer;
  session().AddFilterObserver(&observer);

  std::vector<uint64_t> pids = {1, 2, 3, 4};
  sink()->set_pids(pids);

  constexpr uint64_t kJobKoid1 = 0x1234;
  Job job1(&session(), false);
  job1.AttachForTesting(kJobKoid1, "job-name1");

  ASSERT_TRUE(sink()->requests().empty());

  std::vector<std::string> filters = {"some", "filters"};

  // Sending a first request should send a request and a signal.
  job1.SendAndUpdateFilters(filters);
  {
    ASSERT_EQ(sink()->requests().size(), 1u);
    auto& request = sink()->requests().back();
    EXPECT_EQ(request.job_koid, kJobKoid1);
    CompareVectors(request.filters, filters);

    // There should be no match addiotional signal.
    ASSERT_EQ(observer.filter_matches().size(), 1u);
    auto& filter_match = observer.filter_matches().back();
    EXPECT_EQ(filter_match.job, &job1);
    CompareVectors(filter_match.matched_pids, pids);
  }

  constexpr uint64_t kJobKoid2 = 0x5678;
  Job job2(&session(), false);
  job2.AttachForTesting(kJobKoid2, "job-name2");

  // Sending a with a second job should send a request and a signal.
  job2.SendAndUpdateFilters(filters);
  {
    ASSERT_EQ(sink()->requests().size(), 2u);
    auto& request = sink()->requests().back();
    EXPECT_EQ(request.job_koid, kJobKoid2);
    CompareVectors(request.filters, filters);

    // There should be no match addiotional signal.
    ASSERT_EQ(observer.filter_matches().size(), 2u);
    auto& filter_match = observer.filter_matches().back();
    EXPECT_EQ(filter_match.job, &job2);
    CompareVectors(filter_match.matched_pids, pids);
  }
}

}  // namespace

}  // namespace zxdb
