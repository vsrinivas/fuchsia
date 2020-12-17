// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "archive_accessor_stub.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

using ::testing::ElementsAre;

constexpr char kMessage1Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 1",
        "pid": 200,
        "tid": 300,
        "tag": "tag_1",
        "tag": "tag_2"
      }
    }
  }
]
)JSON";

constexpr char kMessage2Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 2",
        "pid": 200,
        "tid": 300,
        "tag": "tag_2"
      }
    }
  }
]
)JSON";

constexpr char kMessage3Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 3",
        "pid": 200,
        "tid": 300,
        "tag": "tag_3"
      }
    }
  }
]
)JSON";

class LogListenerTest : public gtest::TestLoopFixture {
 public:
  LogListenerTest()
      : service_directory_provider_(sys::testing::ServiceDirectoryProvider()),
        executor_(dispatcher()) {}

  std::shared_ptr<sys::ServiceDirectory>& services() {
    return service_directory_provider_.service_directory();
  }

 protected:
  void SetupLogServer(std::unique_ptr<harvester::ArchiveAccessorStub> server) {
    archive_ = std::move(server);

    service_directory_provider_
        .AddService<fuchsia::diagnostics::ArchiveAccessor>(
            archive_bindings_.GetHandler(archive_.get()));
  }

  std::unique_ptr<harvester::ArchiveAccessorStub> archive_;
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  fidl::BindingSet<fuchsia::diagnostics::ArchiveAccessor> archive_bindings_;
  async::Executor executor_;
};

TEST_F(LogListenerTest, TriggersCallbacks) {
  SetupLogServer(std::make_unique<harvester::ArchiveAccessorStub>(
      std::make_unique<harvester::BatchIteratorStub>(
          std::vector<std::vector<std::string>>(
              {{kMessage1Json, kMessage2Json}, {kMessage3Json}, {}}))));

  harvester::LogListener listener(services());

  int counter = 0;
  listener.Listen([&counter](auto result) { counter++; });

  RunLoopUntilIdle();
  RunLoopFor(zx::sec(1));

  EXPECT_EQ(counter, 2);
}

TEST_F(LogListenerTest, TriggersCallbackWithBatches) {
  SetupLogServer(std::make_unique<harvester::ArchiveAccessorStub>(
      std::make_unique<harvester::BatchIteratorStub>(
          std::vector<std::vector<std::string>>(
              {{kMessage1Json, kMessage2Json}, {kMessage3Json}, {}}))));

  harvester::LogListener listener(services());

  std::vector<std::string> logs = {};
  listener.Listen([&logs](auto result) {
    for (std::string json : result) {
      logs.emplace_back(json);
    }
  });

  RunLoopUntilIdle();

  EXPECT_STREQ(logs[0].c_str(), kMessage1Json);
  EXPECT_STREQ(logs[1].c_str(), kMessage2Json);
  EXPECT_STREQ(logs[2].c_str(), kMessage3Json);
}

TEST_F(LogListenerTest, ClosesOnEmptyBatch) {
  SetupLogServer(std::make_unique<harvester::ArchiveAccessorStub>(
      std::make_unique<harvester::BatchIteratorStub>(
          std::vector<std::vector<std::string>>(
              {{kMessage1Json, kMessage2Json}, {}, {kMessage3Json}}))));

  harvester::LogListener listener(services());

  int counter = 0;
  bool completed = false;
  fit::promise<> promise =
      listener.Listen([&counter](auto result) { counter++; })
          .then([&completed](fit::result<>& result) {
            if (result.is_ok()) {
              completed = true;
            }
          });
  executor_.schedule_task(std::move(promise));
  RunLoopUntilIdle();

  EXPECT_EQ(counter, 1);
  EXPECT_TRUE(completed);
}

TEST_F(LogListenerTest, ClosesOnError) {
  SetupLogServer(std::make_unique<harvester::ArchiveAccessorStub>(
      std::make_unique<harvester::BatchIteratorReturnsErrorStub>()));

  harvester::LogListener listener(services());

  int counter = 0;
  bool completedErr = false;
  fit::promise<> promise =
      listener.Listen([&counter](auto result) { counter++; })
          .then([&completedErr](fit::result<>& result) {
            if (result.is_error()) {
              completedErr = true;
            }
          });
  executor_.schedule_task(std::move(promise));
  RunLoopUntilIdle();

  EXPECT_EQ(counter, 0);
  EXPECT_TRUE(completedErr);
}
