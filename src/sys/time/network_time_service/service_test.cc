// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/service.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <time.h>

#include <thread>

#include "src/lib/files/scoped_temp_dir.h"
#include "src/sys/time/lib/network_time/test/common.h"
#include "src/sys/time/lib/network_time/test/local_roughtime_server.h"
#include "src/sys/time/lib/network_time/time_server_config.h"
#include "third_party/roughtime/protocol.h"

namespace time_external = fuchsia::time::external;
namespace network_time_service {

// Although our test Roughtime server doesn't advance time, there is some error introduced
// in the processing that results in the final reported sample being slightly off.
const int64_t kTimeSpreadNanos = 10'000'000;

const int64_t kExpectedTimeNanos = 7'000'000'000'000;

const uint64_t kTestNanosAfterPoll = 100;

class PushSourceTest : public gtest::TestLoopFixture {
 protected:
  void TearDown() override {
    TestLoopFixture::TearDown();
    if (local_roughtime_server_) {
      local_roughtime_server_->Stop();
    }
    time_service_.reset();
  }

  std::shared_ptr<time_server::LocalRoughtimeServer> local_roughtime_server_ = nullptr;

  // Launch a local Roughtime server in a new thread. Returns the port it is running on.
  uint16_t LaunchLocalRoughtimeServer(const uint8_t* private_key) {
    std::unique_ptr<time_server::LocalRoughtimeServer> local_roughtime_raw =
        time_server::LocalRoughtimeServer::MakeInstance(private_key, 0, 1537485257118'000);
    // The roughtime server thread may outlive the test, so wrap the server in a shared pointer
    // so both test and server threads may safely access it.
    local_roughtime_server_.reset(local_roughtime_raw.release());
    std::shared_ptr<time_server::LocalRoughtimeServer> local_roughtime_server(
        local_roughtime_server_);
    auto roughtime_thread = std::make_unique<std::thread>(
        std::thread([local_roughtime_server]() { local_roughtime_server->Start(); }));
    roughtime_thread->detach();
    uint16_t port_number = local_roughtime_server_->GetPortNumber();
    EXPECT_GT(port_number, 0);
    return port_number;
  }

  // Connect to the PushSource protocol of a TimeServiceImpl. Launches it if not already launched.
  time_external::PushSourcePtr ConnectToService(uint16_t roughtime_port) {
    if (!time_service_) {
      time_server::TimeServerConfig config = ConfigForLocalServer(roughtime_port);
      time_server::RoughTimeServer server = config.ServerList()[0];
      time_server::SystemTimeUpdater updater;
      time_service_.reset(
          new TimeServiceImpl(provider_.TakeContext(), updater, server, dispatcher(),
                              RetryConfig(kTestNanosAfterPoll, kTestNanosAfterPoll)));
    }
    time_external::PushSourcePtr push_source;
    provider_.ConnectToPublicService(push_source.NewRequest());
    return push_source;
  }

 private:
  time_server::TimeServerConfig ConfigForLocalServer(uint16_t port_number) {
    std::string config_json = time_server::local_client_config(port_number);
    std::string client_config_path;
    files::ScopedTempDir temp_dir;
    temp_dir.NewTempFileWithData(config_json, &client_config_path);
    time_server::TimeServerConfig config;
    config.Parse(client_config_path);
    return config;
  }

  std::unique_ptr<TimeServiceImpl> time_service_;
  sys::testing::ComponentContextProvider provider_;
};

TEST_F(PushSourceTest, PushSourceRejectsMultipleClients) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kTestPrivateKey);
  local_roughtime_server_->SetTime(kExpectedTimeNanos / 1000);
  auto first_client = ConnectToService(roughtime_port);

  // Currently the PushSource implementation accepts only one client at a time - see fxb/58068
  auto second_client = ConnectToService(roughtime_port);
  bool error_handler_called = false;
  second_client.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);
    error_handler_called = true;
  });
  second_client->WatchSample([&](time_external::TimeSample sample) { FAIL(); });
  RunLoopUntilIdle();
  EXPECT_TRUE(error_handler_called);

  // original client should still be usable.
  bool call_completed = false;
  first_client->WatchSample([&](time_external::TimeSample sample) { call_completed = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(call_completed);
}

TEST_F(PushSourceTest, PushSourceStateResetOnDisconnect) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kTestPrivateKey);
  local_roughtime_server_->SetTime(kExpectedTimeNanos / 1000);
  time_external::TimeSample original_sample;

  auto first_client = ConnectToService(roughtime_port);
  bool first_call_complete = false;
  first_client->WatchSample([&](time_external::TimeSample sample) {
    original_sample = std::move(sample);
    first_call_complete = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_call_complete);
  first_client.Unbind();
  RunLoopUntilIdle();

  // last sent state for previous client should not be retained, so call should
  // return immediately with the same result.
  bool second_call_complete = false;
  auto second_client = ConnectToService(roughtime_port);
  second_client->WatchSample([&](time_external::TimeSample sample) {
    EXPECT_EQ(original_sample.monotonic(), sample.monotonic());
    EXPECT_EQ(original_sample.utc(), sample.utc());
    second_call_complete = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(second_call_complete);
}

TEST_F(PushSourceTest, WatchSample) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kTestPrivateKey);
  auto proxy = ConnectToService(roughtime_port);
  local_roughtime_server_->SetTime(kExpectedTimeNanos / 1000);

  bool first_call_complete = false;
  zx_time_t mono_before = zx_clock_get_monotonic();
  proxy->WatchSample([&](time_external::TimeSample sample) {
    zx_time_t mono_after = zx_clock_get_monotonic();
    EXPECT_GT(sample.utc(), kExpectedTimeNanos - kTimeSpreadNanos);
    EXPECT_LT(sample.utc(), kExpectedTimeNanos + kTimeSpreadNanos);
    EXPECT_GT(sample.monotonic(), mono_before);
    EXPECT_LT(sample.monotonic(), mono_after);
    first_call_complete = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(first_call_complete);

  bool second_call_complete = false;
  mono_before = zx_clock_get_monotonic();
  proxy->WatchSample([&](time_external::TimeSample sample) {
    zx_time_t mono_after = zx_clock_get_monotonic();
    EXPECT_GT(sample.utc(), kExpectedTimeNanos - kTimeSpreadNanos);
    EXPECT_LT(sample.utc(), kExpectedTimeNanos + kTimeSpreadNanos);
    EXPECT_GT(sample.monotonic(), mono_before);
    EXPECT_LT(sample.monotonic(), mono_after);
    second_call_complete = true;
  });
  // Next poll should only complete after the configured wait period.
  RunLoopFor(zx::nsec(kTestNanosAfterPoll / 2));
  EXPECT_FALSE(second_call_complete);
  RunLoopFor(zx::nsec(kTestNanosAfterPoll));
  EXPECT_TRUE(second_call_complete);
}

TEST_F(PushSourceTest, WatchStatus) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kTestPrivateKey);
  auto proxy = ConnectToService(roughtime_port);
  bool call_complete = false;
  proxy->WatchStatus([&](time_external::Status status) {
    EXPECT_EQ(status, time_external::Status::OK);
    call_complete = true;
  });
  RunLoopUntilIdle();

  // Second call should not complete since status should not change.
  // A call to WatchSample made afterwards should complete while the status watch is active.
  proxy->WatchStatus([&](time_external::Status status) { FAIL(); });
  bool sample_call_complete = false;
  proxy->WatchSample([&](time_external::TimeSample sample) { sample_call_complete = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(sample_call_complete);
}

TEST_F(PushSourceTest, WatchStatusUnhealthy) {
  // Connect to a roughtime server signing with a bad key to simulate unhealthy.
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kWrongPrivateKey);
  auto proxy = ConnectToService(roughtime_port);
  // First call always indicates OK
  proxy->WatchStatus(
      [&](time_external::Status status) { EXPECT_EQ(status, time_external::Status::OK); });
  RunLoopUntilIdle();

  // Obtaining a sample should fail and result in reporting an unhealthy status.
  proxy->WatchSample([&](time_external::TimeSample sample) { FAIL(); });
  bool second_call_complete = false;
  proxy->WatchStatus([&](time_external::Status status) {
    EXPECT_EQ(status, time_external::Status::PROTOCOL);
    second_call_complete = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(second_call_complete);
}

TEST_F(PushSourceTest, ChannelClosedOnConcurrentWatchStatus) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kWrongPrivateKey);
  bool error_handler_called = false;
  auto proxy = ConnectToService(roughtime_port);
  proxy.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    error_handler_called = true;
  });
  // first call always completes immediately
  proxy->WatchStatus(
      [&](time_external::Status status) { EXPECT_EQ(status, time_external::Status::OK); });
  RunLoopUntilIdle();

  proxy->WatchStatus([&](time_external::Status sample) { FAIL(); });
  proxy->WatchStatus([&](time_external::Status sample) { FAIL(); });
  RunLoopUntilIdle();
  EXPECT_TRUE(error_handler_called);
}

TEST_F(PushSourceTest, ChannelClosedOnConcurrentWatchSample) {
  uint16_t roughtime_port = LaunchLocalRoughtimeServer(time_server::kWrongPrivateKey);
  bool error_handler_called = false;
  auto proxy = ConnectToService(roughtime_port);
  proxy.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    error_handler_called = true;
  });

  // First call should not complete as server is returning bad responses.
  proxy->WatchSample([&](time_external::TimeSample sample) { FAIL(); });
  proxy->WatchSample([&](time_external::TimeSample sample) { FAIL(); });
  RunLoopUntilIdle();
  EXPECT_TRUE(error_handler_called);
}

}  // namespace network_time_service
