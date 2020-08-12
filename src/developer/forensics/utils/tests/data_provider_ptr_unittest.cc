// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/data_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace {

using fuchsia::feedback::Snapshot;

constexpr zx::duration kDefaultTimeout = zx::sec(35);

class DataProviderPtrTest : public UnitTestFixture {
 public:
  DataProviderPtrTest()
      : UnitTestFixture(), executor_(dispatcher()), data_provider_ptr_(dispatcher(), services()) {}

 protected:
  void SetUpDataProviderServer(std::unique_ptr<stubs::DataProviderBase> data_provider_server) {
    data_provider_server_ = std::move(data_provider_server);
    if (data_provider_server_) {
      InjectServiceProvider(data_provider_server_.get());
    }
  }

  void CloseConnection() { data_provider_server_->CloseConnection(); }

  bool is_server_bound() { return data_provider_server_->IsBound(); }

  std::vector<::fit::result<Snapshot, Error>> GetSnapshot(const size_t num_parallel_calls) {
    std::vector<::fit::result<Snapshot, Error>> results(num_parallel_calls);
    for (auto& result : results) {
      executor_.schedule_task(data_provider_ptr_.GetSnapshot(kDefaultTimeout)
                                  .then([&](::fit::result<Snapshot, Error>& snapshot) {
                                    result = std::move(snapshot);
                                  }));
    }
    RunLoopUntilIdle();
    return results;
  }

 protected:
  async::Executor executor_;
  fidl::DataProviderPtr data_provider_ptr_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
};

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
  EXPECT_TRUE(results[0].is_error());
  EXPECT_EQ(results[0].error(), Error::kConnectionError);
}

TEST_F(DataProviderPtrTest, Fail_OnServerTakingTooLong) {
  const size_t num_calls = 1u;

  SetUpDataProviderServer(std::make_unique<stubs::DataProviderNeverReturning>());

  std::vector<::fit::result<Snapshot, Error>> results = GetSnapshot(num_calls);
  RunLoopFor(kDefaultTimeout);

  ASSERT_EQ(results.size(), num_calls);
  EXPECT_TRUE(results[0].is_error());
  EXPECT_EQ(results[0].error(), Error::kTimeout);
}

}  // namespace
}  // namespace forensics
