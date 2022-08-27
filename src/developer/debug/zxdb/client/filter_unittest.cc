// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include <gtest/gtest.h>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

namespace {

using debug::MessageLoop;

class FilterSink : public RemoteAPI {
 public:
  void UpdateFilter(const debug_ipc::UpdateFilterRequest& request,
                    fit::callback<void(const Err&, debug_ipc::UpdateFilterReply)> cb) override {
    filter_requests_.push_back(request);

    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::UpdateFilterReply()); });
  }

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    debug_ipc::AttachReply reply;
    reply.koid = request.koid;
    reply.name = "test";

    MessageLoop::Current()->PostTask(FROM_HERE,
                                     [cb = std::move(cb), reply]() mutable { cb(Err(), reply); });
  }

  std::vector<debug_ipc::UpdateFilterRequest> filter_requests_;
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

  // There is no filter to send yet.
  ASSERT_EQ(sink().filter_requests_.size(), 0u);

  filter->SetType(debug_ipc::Filter::Type::kProcessNameSubstr);
  filter->SetPattern("foo");
  MessageLoop::Current()->RunUntilNoTasks();

  // There should be a filter request.
  ASSERT_EQ(sink().filter_requests_.size(), 1u);
  ASSERT_EQ(sink().filter_requests_[0].filters.size(), 1u);
  ASSERT_EQ(sink().filter_requests_[0].filters[0].pattern, "foo");

  // Deleting the filter should clean up the filters.
  session().system().DeleteFilter(filter);
  MessageLoop::Current()->RunUntilNoTasks();

  // There should be a filter request.
  ASSERT_EQ(sink().filter_requests_.size(), 2u);
  EXPECT_TRUE(sink().filter_requests_[1].filters.empty());
}

}  // namespace zxdb
