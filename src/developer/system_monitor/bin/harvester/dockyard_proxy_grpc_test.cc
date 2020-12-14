// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_grpc.h"

#include <lib/zx/clock.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_dockyard_stub.h"
#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"

class DockyardProxyGrpcTest : public ::testing::Test {
 public:
  void SetUp() {}
};

uint64_t getUtcTime() {
  // TODO(fxbug.dev/65180): Add a check for ZX_CLOCK_STARTED.
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

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

TEST(DockyardClientTest, SendLogTest) {
  const std::string logs1 = "[{hello: world}, {foo: bar}]";
  const std::string logs2 = "[{hello: world}, {foo: bar}]";
  std::vector<const std::string> batch = {logs1, logs1};

  uint64_t startTime = getUtcTime();
  uint64_t startMono = getMonotonicTime();

  std::unique_ptr<MockDockyardStub> mock_stub =
      std::make_unique<MockDockyardStub>();

  auto mock_reader_writer =
      new grpc::testing::MockClientReaderWriter<LogBatch, EmptyMessage>();
  LogBatch logs;

  EXPECT_CALL(*mock_stub, SendLogsRaw(_)).WillOnce(Return(mock_reader_writer));
  EXPECT_CALL(*mock_reader_writer, Write(_, _))
      .Times(1)
      .WillRepeatedly(DoAll(SaveArg<0>(&logs), Return(true)));
  EXPECT_CALL(*mock_reader_writer, WritesDone());
  EXPECT_CALL(*mock_reader_writer, Finish())
      .WillOnce(Return(::grpc::Status::OK));

  harvester::DockyardProxyGrpc dockyard_proxy(std::move(mock_stub));
  dockyard_proxy.SendLogs(batch);

  auto log_json = logs.log_json();
  for (auto i = 0; i < log_json.size(); i++) {
    EXPECT_EQ(log_json[i].json(), batch[i]);
  }
  EXPECT_THAT(logs.time(), AllOf(Gt(startTime), Lt(getUtcTime())));
  EXPECT_THAT(logs.mono(), AllOf(Gt(startMono), Lt(getMonotonicTime())));
}

TEST_F(DockyardProxyGrpcTest, BuildLogBatch) {
  const std::string logs1 = "[{hello: world}, {foo: bar}]";
  const std::string logs2 = "[{hello: world}, {foo: bar}]";
  std::vector<const std::string> batch = {logs1, logs1};
  uint64_t mono = 100;
  uint64_t time = 1000;

  dockyard_proto::LogBatch result =
      harvester::internal::BuildLogBatch(batch, mono, time);

  auto log_json = result.log_json();
  for (auto i = 0; i < log_json.size(); i++) {
    EXPECT_EQ(log_json[i].json(), batch[i]);
  }
  EXPECT_EQ(result.mono(), mono);
  EXPECT_EQ(result.time(), time);
}
