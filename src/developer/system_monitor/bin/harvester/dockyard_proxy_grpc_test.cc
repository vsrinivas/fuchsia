// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_grpc.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_dockyard_stub.h"
#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

class DockyardProxyGrpcTest : public gtest::TestLoopFixture {
 public:
  void SetUp() {
    zx_clock_create_args_v1_t clock_args{.backstop_time = 0};
    FX_CHECK(
        zx::clock::create(ZX_CLOCK_ARGS_VERSION(1) | ZX_CLOCK_OPT_AUTO_START,
                          &clock_args, &clock_handle_) == ZX_OK);
  }

 protected:
  zx::clock clock_handle_;
};

uint64_t getMonotonicTime() { return zx::clock::get_monotonic().get(); }

TEST_F(DockyardProxyGrpcTest, ExtractPathsFromSampleList) {
  harvester::SampleList in = {
      {"path1", 0UL},
      {"path2", 19UL},
      {"path1", 42UL},
  };
  std::vector<const std::string*> out(in.size());

  harvester::internal::ExtractPathsFromSampleList(&out, in);

  EXPECT_EQ(*out[0], "path1");
  EXPECT_EQ(*out[1], "path2");
  EXPECT_EQ(*out[2], "path1");
}

TEST_F(DockyardProxyGrpcTest, BuildSampleListById) {
  std::vector<dockyard::DockyardId> id_list = {13, 8, 13};
  harvester::SampleList sample_list = {
      {"path1", 0UL},
      {"path2", 19UL},
      {"path1", 42UL},
  };
  harvester::SampleListById out(id_list.size());

  harvester::internal::BuildSampleListById(&out, id_list, sample_list);

  EXPECT_EQ(out[0], std::make_pair(13UL, 0UL));
  EXPECT_EQ(out[1], std::make_pair(8UL, 19UL));
  EXPECT_EQ(out[2], std::make_pair(13UL, 42UL));
}

TEST_F(DockyardProxyGrpcTest, SendLogTest) {
  const std::string logs1 = "[{hello: world}, {foo: bar}]";
  const std::string logs2 = "[{hello: world}, {foo: bar}]";
  std::vector<const std::string> batch = {logs1, logs1};

  uint64_t startMono = getMonotonicTime();

  std::unique_ptr<MockDockyardStub> mock_stub =
      std::make_unique<MockDockyardStub>();

  auto mock_reader_writer =
      new grpc::testing::MockClientReaderWriter<dockyard_proto::LogBatch,
                                                EmptyMessage>();
  dockyard_proto::LogBatch logs;

  EXPECT_CALL(*mock_stub, SendLogsRaw(_)).WillOnce(Return(mock_reader_writer));
  EXPECT_CALL(*mock_reader_writer, Write(_, _))
      .Times(1)
      .WillRepeatedly(DoAll(SaveArg<0>(&logs), Return(true)));
  EXPECT_CALL(*mock_reader_writer, WritesDone());
  EXPECT_CALL(*mock_reader_writer, Finish())
      .WillOnce(Return(::grpc::Status::OK));

  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();
  constexpr timekeeper::time_utc log_time(
      (zx::hour(9) + zx::min(31) + zx::sec(42)).get());
  test_clock->Set(log_time);

  std::unique_ptr<harvester::FuchsiaClock> clock =
      std::make_unique<harvester::FuchsiaClock>(
          dispatcher(), std::move(test_clock),
          zx::unowned_clock(clock_handle_.get_handle()));
  clock->WaitForStart([](zx_status_t status) {});

  harvester::DockyardProxyGrpc dockyard_proxy(std::move(mock_stub),
                                              std::move(clock));
  dockyard_proxy.SendLogs(batch);

  auto log_json = logs.log_json();
  for (auto i = 0; i < log_json.size(); i++) {
    EXPECT_EQ(log_json[i].json(), batch[i]);
  }
  EXPECT_EQ(logs.time(), (uint64_t)log_time.get());
  EXPECT_THAT(logs.monotonic_time(),
              AllOf(Gt(startMono), Lt(getMonotonicTime())));
}

TEST_F(DockyardProxyGrpcTest, BuildLogBatch) {
  const std::string logs1 = "[{hello: world}, {foo: bar}]";
  const std::string logs2 = "[{hello: world}, {foo: bar}]";
  std::vector<const std::string> batch = {logs1, logs1};
  uint64_t monotonic_time = 100;
  uint64_t time = 1000;

  dockyard_proto::LogBatch result =
      harvester::internal::BuildLogBatch(batch, monotonic_time, time);

  auto log_json = result.log_json();
  for (auto i = 0; i < log_json.size(); i++) {
    EXPECT_EQ(log_json[i].json(), batch[i]);
  }
  EXPECT_EQ(result.monotonic_time(), monotonic_time);
  EXPECT_EQ(result.time(), time);
}

TEST_F(DockyardProxyGrpcTest, SendUtcClockStartedTest) {
  std::unique_ptr<MockDockyardStub> mock_stub =
      std::make_unique<MockDockyardStub>();

  dockyard_proto::UtcClockStartedRequest request;

  EXPECT_CALL(*mock_stub, UtcClockStarted(_, _, _))
      .Times(1)
      .WillOnce(DoAll(SaveArg<1>(&request), Return(::grpc::Status::OK)));
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();
  constexpr timekeeper::time_utc start_time(
      (zx::hour(9) + zx::min(31) + zx::sec(42)).get());
  test_clock->Set(start_time);

  std::unique_ptr<harvester::FuchsiaClock> clock =
      std::make_unique<harvester::FuchsiaClock>(
          dispatcher(), std::move(test_clock),
          zx::unowned_clock(clock_handle_.get_handle()));

  harvester::DockyardProxyGrpc dockyard_proxy(std::move(mock_stub),
                                              std::move(clock));
  dockyard_proxy.Init();

  EXPECT_EQ(request.device_time_ns(), (uint64_t)start_time.get());
}
