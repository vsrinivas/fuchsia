// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/data_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using fuchsia::feedback::Snapshot;
using testing::Contains;

constexpr zx::duration kDefaultTimeout = zx::sec(35);
constexpr zx::duration kDelta = zx::sec(5);

class DataProviderPtrTest : public UnitTestFixture {
 public:
  DataProviderPtrTest()
      : UnitTestFixture(),
        clock_(new timekeeper::TestClock()),
        executor_(dispatcher()),
        data_provider_ptr_(dispatcher(), services(), kDelta,
                           std::unique_ptr<timekeeper::TestClock>(clock_)) {}

 protected:
  void SetUpDataProviderServer(std::unique_ptr<stubs::DataProviderBase> data_provider_server) {
    data_provider_server_ = std::move(data_provider_server);
    if (data_provider_server_) {
      InjectServiceProvider(data_provider_server_.get());
    }
  }

  void CloseConnection() { data_provider_server_->CloseConnection(); }

  bool is_server_bound() { return data_provider_server_->IsBound(); }

  std::vector<::fit::result<Snapshot, Error>> GetSnapshot(const size_t num_parallel_calls,
                                                          const bool run_loop = true) {
    std::vector<::fit::result<Snapshot, Error>> results(num_parallel_calls);
    GetSnapshot(num_parallel_calls, &results, run_loop);
    return results;
  }

  // Out parameter variant of GetSnapshot. This is needed because if the loop isn't run in
  // GetSnapshot, the captured references to each result may move in memory.
  void GetSnapshot(const size_t num_parallel_calls,
                   std::vector<::fit::result<Snapshot, Error>>* results,
                   const bool run_loop = true) {
    results->resize(num_parallel_calls);
    for (auto& result : *results) {
      executor_.schedule_task(data_provider_ptr_.GetSnapshot(kDefaultTimeout)
                                  .then([&](::fit::result<Snapshot, Error>& snapshot) {
                                    result = std::move(snapshot);
                                  }));
    }
    if (run_loop) {
      RunLoopUntilIdle();
    }
  }

  timekeeper::TestClock* clock_;
  async::Executor executor_;
  DataProviderPtr data_provider_ptr_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
};

TEST_F(DataProviderPtrTest, Check_PoolsCalls) {
  const size_t num_pools = 2u;
  const size_t pool_size = 5u;

  SetUpDataProviderServer(std::make_unique<stubs::DataProviderTracksNumCalls>(num_pools));

  std::vector<::fit::result<Snapshot, Error>> pool1_results;
  std::vector<::fit::result<Snapshot, Error>> additional_pool1_results;
  std::vector<::fit::result<Snapshot, Error>> pool2_results;

  GetSnapshot(pool_size, &pool1_results, /*run_loop=*/false);

  // Increment the clock less that |kDelta| to ensure new snapsnot requests are pooled with old
  // ones.
  clock_->Set(clock_->Now() + kDelta / 2);
  GetSnapshot(pool_size, &additional_pool1_results, /*run_loop=*/false);

  // Increment the clock so |kDelta| has elapsed since the first pool was created to ensure a new
  // pool is made.
  clock_->Set(clock_->Now() + kDelta / 2);
  GetSnapshot(pool_size, &pool2_results, /*run_loop=*/false);

  RunLoopUntilIdle();

  ASSERT_EQ(pool1_results.size(), pool_size);
  for (const auto& result : pool1_results) {
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().has_annotations());
    EXPECT_THAT(
        result.value().annotations(),
        Contains(MatchesAnnotation("debug.snapshot.pool.size", std::to_string(pool_size * 2))));
  }

  ASSERT_EQ(additional_pool1_results.size(), pool_size);
  for (const auto& result : additional_pool1_results) {
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().has_annotations());
    EXPECT_THAT(
        result.value().annotations(),
        Contains(MatchesAnnotation("debug.snapshot.pool.size", std::to_string(pool_size * 2))));
  }

  ASSERT_EQ(pool2_results.size(), pool_size);
  for (const auto& result : pool2_results) {
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().has_annotations());
    EXPECT_THAT(result.value().annotations(),
                Contains(MatchesAnnotation("debug.snapshot.pool.size", std::to_string(pool_size))));
  }
}

TEST_F(DataProviderPtrTest, Check_ConnectionIsReused) {
  const size_t num_calls = 5u;
  SetUpDataProviderServer(std::make_unique<stubs::DataProviderTracksNumConnections>(1u));

  const std::vector<::fit::result<Snapshot, Error>> results = GetSnapshot(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_ok());
  }

  EXPECT_FALSE(is_server_bound());
}

TEST_F(DataProviderPtrTest, Check_ReconnectsCorrectly) {
  const size_t num_calls = 5u;
  SetUpDataProviderServer(std::make_unique<stubs::DataProviderTracksNumConnections>(2u));

  std::vector<::fit::result<Snapshot, Error>> results = GetSnapshot(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_ok());
  }

  EXPECT_FALSE(is_server_bound());

  results.clear();
  results = GetSnapshot(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_ok());
  }

  EXPECT_FALSE(is_server_bound());
}

TEST_F(DataProviderPtrTest, Fail_OnNoServer) {
  const size_t num_calls = 1u;

  // We pass a nullptr stub so there will be no fuchsia.feedback.DataProvider service to connect to.
  SetUpDataProviderServer(nullptr);

  std::vector<::fit::result<Snapshot, Error>> results = GetSnapshot(num_calls);
  ASSERT_EQ(results.size(), num_calls);
  EXPECT_TRUE(results[0].is_ok());
  EXPECT_THAT(
      results[0].value().annotations(),
      Contains(MatchesAnnotation("debug.snapshot.error", ToReason(Error::kConnectionError))));
}

TEST_F(DataProviderPtrTest, Fail_OnServerTakingTooLong) {
  const size_t num_calls = 1u;

  SetUpDataProviderServer(std::make_unique<stubs::DataProviderNeverReturning>());

  std::vector<::fit::result<Snapshot, Error>> results = GetSnapshot(num_calls);
  RunLoopFor(kDefaultTimeout);

  ASSERT_EQ(results.size(), num_calls);
  EXPECT_THAT(results[0].value().annotations(),
              Contains(MatchesAnnotation("debug.snapshot.error", ToReason(Error::kTimeout))));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
