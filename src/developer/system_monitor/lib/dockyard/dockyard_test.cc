// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

#include <climits>

#include <gtest/gtest.h>

#include "src/developer/system_monitor/lib/dockyard/dockyard_service_impl.h"
#include "src/developer/system_monitor/lib/dockyard/test_sample_generator.h"

namespace dockyard {

namespace {

// A server address that gets the OS to choose a free port.
const char kgRPCServerAddressForTest[] = "0.0.0.0:0";

}

class SystemMonitorDockyardTest : public ::testing::Test {
 public:
  void SetUp() {
    // Initialize to distinct values for testing.
    EXPECT_EQ(nullptr, dockyard_.SetDockyardPathsHandler(std::bind(
                           &SystemMonitorDockyardTest::TestPathsCallback, this,
                           std::placeholders::_1, std::placeholders::_2)));
    // Add some samples.
    dockyard_.AddSamples(dockyard_.GetDockyardId("cpu0"),
                         {{10ULL, 8ULL}, {200ULL, 10ULL}, {300ULL, 20ULL}});
    dockyard_.AddSamples(dockyard_.GetDockyardId("cpu1"), {{10ULL, 3ULL},
                                                           {20ULL, 4ULL},
                                                           {80ULL, 5ULL},
                                                           {81ULL, 50ULL},
                                                           {100ULL, 10ULL},
                                                           {200ULL, 100ULL},
                                                           {300ULL, 80ULL},
                                                           {400ULL, 100ULL},
                                                           {500ULL, 50ULL}});
    dockyard_.AddSamples(
        dockyard_.GetDockyardId("cpu2"),
        {
            {100ULL, 3ULL},  {105ULL, 4ULL},   {110ULL, 5ULL},  {115ULL, 50ULL},
            {120ULL, 90ULL}, {125ULL, 100ULL}, {130ULL, 80ULL}, {135ULL, 45ULL},
            {140ULL, 44ULL}, {150ULL, 40ULL},  {155ULL, 30ULL}, {160ULL, 12ULL},
            {165ULL, 10ULL}, {170ULL, 8ULL},   {175ULL, 5ULL},  {180ULL, 3ULL},
            {185ULL, 5ULL},  {190ULL, 15ULL},  {195ULL, 50ULL},
        });
    dockyard_.AddSamples(dockyard_.GetDockyardId("cpu3"), {{100ULL, 103ULL},
                                                           {105ULL, 104ULL},
                                                           {110ULL, 107ULL},
                                                           {115ULL, 112ULL},
                                                           {120ULL, 112ULL},
                                                           {130ULL, 122ULL},
                                                           {135ULL, 127ULL},
                                                           {140ULL, 130ULL},
                                                           {150ULL, 132ULL},
                                                           {165ULL, 132ULL},
                                                           {170ULL, 133ULL},
                                                           {175ULL, 135ULL},
                                                           {180ULL, 138ULL},
                                                           {185ULL, 142ULL},
                                                           {190ULL, 147ULL},
                                                           {195ULL, 148ULL}});
  }

  // Returns whether the gRPC server is listening for Harvester connections.
  bool IsGrpcServerActive() {
    return !!dockyard_.grpc_server_ && dockyard_.server_thread_.joinable();
  }

  // TODO(fxbug.dev/37317): add further tests for paths.
  void TestPathsCallback(const std::vector<PathInfo>& add,
                         const std::vector<uint32_t>& remove) {}

  Dockyard dockyard_;
};

namespace {

TEST_F(SystemMonitorDockyardTest, SetsCallback) {
  // No pending requests.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SlopeValuesMono) {
  constexpr uint64_t SAMPLE_COUNT = 20;
  RandomSampleGenerator gen;
  gen.dockyard_id = dockyard_.GetDockyardId("fake0");
  gen.seed = 1234;
  gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
  gen.start = 100;
  gen.finish = 500;
  gen.value_style = RandomSampleGenerator::VALUE_STYLE_MONO_INCREASE;
  gen.value_min = 10;
  gen.value_max = 100;
  gen.sample_count = SAMPLE_COUNT / 4;
  GenerateRandomSamples(gen, &dockyard_);

  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.flags = StreamSetsRequest::SLOPE;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::HIGHEST_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(0ULL, response.lowest_value);
        EXPECT_EQ(dockyard::SLOPE_LIMIT, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][1]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][2]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][3]);
        EXPECT_EQ(225000ULL, response.data_sets[0][4]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][5]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsRequestNormal) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetSampleStreams(
      std::move(request), [](const SampleStreamsRequest& request,
                             const SampleStreamsResponse& response) {
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(4UL, response.data_sets[0].size());
        EXPECT_EQ(100ULL, response.data_sets[0][0].first);
        EXPECT_EQ(10ULL, response.data_sets[0][0].second);
        EXPECT_EQ(200ULL, response.data_sets[0][1].first);
        EXPECT_EQ(100ULL, response.data_sets[0][1].second);
        EXPECT_EQ(300ULL, response.data_sets[0][2].first);
        EXPECT_EQ(80ULL, response.data_sets[0][2].second);
        EXPECT_EQ(400ULL, response.data_sets[0][3].first);
        EXPECT_EQ(100ULL, response.data_sets[0][3].second);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsRequestNearValues) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 25;
  request.end_time_ns = 81;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetSampleStreams(std::move(request),
                             [](const SampleStreamsRequest& request,
                                const SampleStreamsResponse& response) {
                               ASSERT_EQ(1UL, response.data_sets.size());
                               ASSERT_EQ(1UL, response.data_sets[0].size());
                               EXPECT_EQ(80ULL, response.data_sets[0][0].first);
                               EXPECT_EQ(5ULL, response.data_sets[0][0].second);
                             });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsGenerated) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  RandomSampleGenerator gen;
  gen.dockyard_id = dockyard_.GetDockyardId("fake0");
  gen.seed = 1234;
  gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
  gen.start = 100;
  gen.finish = 500;
  gen.value_style = RandomSampleGenerator::VALUE_STYLE_MONO_INCREASE;
  gen.value_min = 10;
  gen.value_max = 100;
  gen.sample_count = SAMPLE_COUNT;
  GenerateRandomSamples(gen, &dockyard_);

  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 100;
  // this is the exact time of the fifth sample
  request.end_time_ns = 420;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0"));
  dockyard_.GetSampleStreams(
      std::move(request), [](const SampleStreamsRequest& request,
                             const SampleStreamsResponse& response) {
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT - 1, response.data_sets[0].size());
        // Check the samples themselves.
        std::vector<std::pair<SampleTimeNs, SampleValue>> expected = {
            {100ULL, 10UL}, {180ULL, 28ULL}, {260ULL, 46ULL}, {340ULL, 64ULL}};
        int data_set_index = 0;
        for (const auto& [timestamp, value] : expected) {
          EXPECT_EQ(timestamp, response.data_sets[0][data_set_index].first);
          EXPECT_EQ(value, response.data_sets[0][data_set_index].second);
          data_set_index++;
        }
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsRequestAfterSamples) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 600;
  request.end_time_ns = 700;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetSampleStreams(std::move(request),
                             [](const SampleStreamsRequest& request,
                                const SampleStreamsResponse& response) {
                               ASSERT_EQ(1UL, response.data_sets.size());
                               ASSERT_EQ(0UL, response.data_sets[0].size());
                             });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsRequestBeforeSamples) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 10;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetSampleStreams(std::move(request),
                             [](const SampleStreamsRequest& request,
                                const SampleStreamsResponse& response) {
                               ASSERT_EQ(1UL, response.data_sets.size());
                               ASSERT_EQ(0UL, response.data_sets[0].size());
                             });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsMissing) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("does-not-exist"));
  dockyard_.GetSampleStreams(std::move(request),
                             [](const SampleStreamsRequest& request,
                                const SampleStreamsResponse& response) {
                               ASSERT_EQ(1UL, response.data_sets.size());
                               ASSERT_EQ(0UL, response.data_sets[0].size());
                             });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SampleStreamsRequestNormalMultiple) {
  // Add pending request.
  SampleStreamsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu0"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetSampleStreams(
      std::move(request), [](const SampleStreamsRequest& request,
                             const SampleStreamsResponse& response) {
        ASSERT_EQ(2UL, response.data_sets.size());

        ASSERT_EQ(2UL, response.data_sets[0].size());
        EXPECT_EQ(200ULL, response.data_sets[0][0].first);
        EXPECT_EQ(10ULL, response.data_sets[0][0].second);
        EXPECT_EQ(300ULL, response.data_sets[0][1].first);
        EXPECT_EQ(20ULL, response.data_sets[0][1].second);

        ASSERT_EQ(4UL, response.data_sets[1].size());
        EXPECT_EQ(10ULL, response.data_sets[1][0].second);
        EXPECT_EQ(100ULL, response.data_sets[1][1].second);
        EXPECT_EQ(80ULL, response.data_sets[1][2].second);
        EXPECT_EQ(100ULL, response.data_sets[1][3].second);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SlopeCpu3Highest) {
  constexpr uint64_t SAMPLE_COUNT = 20;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.flags = StreamSetsRequest::SLOPE;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::HIGHEST_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu3"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(0ULL, response.lowest_value);
        EXPECT_EQ(1000000ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(200000ULL, response.data_sets[0][1]);
        EXPECT_EQ(600000ULL, response.data_sets[0][2]);
        EXPECT_EQ(1000000ULL, response.data_sets[0][3]);
        EXPECT_EQ(0ULL, response.data_sets[0][4]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][5]);
        EXPECT_EQ(1000000ULL, response.data_sets[0][6]);
        EXPECT_EQ(1000000ULL, response.data_sets[0][7]);
        EXPECT_EQ(600000ULL, response.data_sets[0][8]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
        EXPECT_EQ(200000ULL, response.data_sets[0][10]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][11]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][12]);
        EXPECT_EQ(0ULL, response.data_sets[0][13]);
        EXPECT_EQ(200000ULL, response.data_sets[0][14]);
        EXPECT_EQ(400000ULL, response.data_sets[0][15]);
        EXPECT_EQ(600000ULL, response.data_sets[0][16]);
        EXPECT_EQ(800000ULL, response.data_sets[0][17]);
        EXPECT_EQ(1000000ULL, response.data_sets[0][18]);
        EXPECT_EQ(200000ULL, response.data_sets[0][19]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SlopeCpu3Average) {
  constexpr uint64_t SAMPLE_COUNT = 7;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.flags = StreamSetsRequest::SLOPE;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu3"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(0ULL, response.lowest_value);
        EXPECT_EQ(1000000ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(571428ULL, response.data_sets[0][1]);
        EXPECT_EQ(1000000ULL, response.data_sets[0][2]);
        EXPECT_EQ(428571ULL, response.data_sets[0][3]);
        EXPECT_EQ(0ULL, response.data_sets[0][4]);
        EXPECT_EQ(285714ULL, response.data_sets[0][5]);
        EXPECT_EQ(642857ULL, response.data_sets[0][6]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RawPastEndResponse) {
  constexpr uint64_t SAMPLE_COUNT = 10;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 1000;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu0"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(8ULL, response.lowest_value);
        EXPECT_EQ(20ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(8ULL, response.data_sets[0][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][1]);
        EXPECT_EQ(10ULL, response.data_sets[0][2]);
        EXPECT_EQ(20ULL, response.data_sets[0][3]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][4]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][5]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][6]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][7]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][8]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RawSparseResponse) {
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 300;
  request.sample_count = 10;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu0"));
  dockyard_.GetStreamSets(
      std::move(request),
      [](const StreamSetsRequest& request, const StreamSetsResponse& response) {
        EXPECT_EQ(8ULL, response.lowest_value);
        EXPECT_EQ(20ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(10UL, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(8ULL, response.data_sets[0][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][1]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][2]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][3]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][4]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][5]);
        EXPECT_EQ(10ULL, response.data_sets[0][6]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][7]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][8]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpu1) {
  constexpr uint64_t SAMPLE_COUNT = 10;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 50;
  request.end_time_ns = 450;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(27ULL, response.data_sets[0][0]);
        EXPECT_EQ(10ULL, response.data_sets[0][1]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][2]);
        EXPECT_EQ(100ULL, response.data_sets[0][3]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][4]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][5]);
        EXPECT_EQ(80ULL, response.data_sets[0][6]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][7]);
        EXPECT_EQ(100ULL, response.data_sets[0][8]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(15ULL, response.data_sets[0][0]);
        EXPECT_EQ(78ULL, response.data_sets[0][1]);
        EXPECT_EQ(38ULL, response.data_sets[0][2]);
        EXPECT_EQ(8ULL, response.data_sets[0][3]);
        EXPECT_EQ(18ULL, response.data_sets[0][4]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpus012) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu0"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(3UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[1].size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[2].size());
        // Check the samples themselves.
        // CPU 0.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][1]);
        // CPU 1.
        EXPECT_EQ(10ULL, response.data_sets[1][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[1][1]);
        // CPU 2.
        EXPECT_EQ(46ULL, response.data_sets[2][0]);
        EXPECT_EQ(17ULL, response.data_sets[2][1]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, HighDataSetsCpus12) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 20;
  request.end_time_ns = 150;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::HIGHEST_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(2UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        // CPU 1.
        EXPECT_EQ(50ULL, response.data_sets[0][0]);
        EXPECT_EQ(10ULL, response.data_sets[0][1]);
        // CPU 2.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[1][0]);
        EXPECT_EQ(100ULL, response.data_sets[1][1]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, LowDataSetsCpus12) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 20;
  request.end_time_ns = 150;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::LOWEST_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(2UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        // CPU 1.
        EXPECT_EQ(4ULL, response.data_sets[0][0]);
        EXPECT_EQ(10ULL, response.data_sets[0][1]);
        // CPU 2.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[1][0]);
        EXPECT_EQ(3ULL, response.data_sets[1][1]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, NormalizedDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.flags = StreamSetsRequest::NORMALIZE;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(123711ULL, response.data_sets[0][0]);
        EXPECT_EQ(773195ULL, response.data_sets[0][1]);
        EXPECT_EQ(360824ULL, response.data_sets[0][2]);
        EXPECT_EQ(51546ULL, response.data_sets[0][3]);
        EXPECT_EQ(154639ULL, response.data_sets[0][4]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SmoothDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::WIDE_SMOOTHING;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(47ULL, response.data_sets[0][0]);
        EXPECT_EQ(44ULL, response.data_sets[0][1]);
        EXPECT_EQ(42ULL, response.data_sets[0][2]);
        EXPECT_EQ(20ULL, response.data_sets[0][3]);
        EXPECT_EQ(13ULL, response.data_sets[0][4]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SculptedDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::SCULPTING;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(3ULL, response.data_sets[0][0]);
        EXPECT_EQ(100ULL, response.data_sets[0][1]);
        EXPECT_EQ(30ULL, response.data_sets[0][2]);
        EXPECT_EQ(5ULL, response.data_sets[0][3]);
        EXPECT_EQ(3ULL, response.data_sets[0][4]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RandomSamples) {
  constexpr uint64_t SAMPLE_COUNT = 40;
  RandomSampleGenerator gen;
  gen.dockyard_id = dockyard_.GetDockyardId("fake0");
  gen.seed = 1234;
  gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
  gen.start = 100;
  gen.finish = 500;
  gen.value_style = RandomSampleGenerator::VALUE_STYLE_SINE_WAVE;
  gen.value_min = 10;
  gen.value_max = 100;
  gen.sample_count = SAMPLE_COUNT;
  GenerateRandomSamples(gen, &dockyard_);

  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(10ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(55ULL, response.data_sets[0][0]);
        EXPECT_EQ(99ULL, response.data_sets[0][9]);
        EXPECT_EQ(29ULL, response.data_sets[0][19]);
        EXPECT_EQ(29ULL, response.data_sets[0][29]);
        EXPECT_EQ(99ULL, response.data_sets[0][39]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, NegativeSlope) {
  // The timestamps in this fake sample stream increase by 10 in each successive
  // Sample. The time data on the second entry of each Sample should increase by
  // 0..10 in each successive sample. A value that is lower than the prior entry
  // has a negative slope. E.g. the change from 10ULL to 7ULL is a downward
  // trend (with a negative slope).
  dockyard_.AddSamples(dockyard_.GetDockyardId("data"), {{100ULL, 5ULL},
                                                         {110ULL, 10ULL},
                                                         {120ULL, 7ULL},
                                                         {130ULL, 15ULL},
                                                         {140ULL, 16ULL},
                                                         {150ULL, 25ULL}});

  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 160;
  request.flags = StreamSetsRequest::SLOPE;
  request.sample_count = 6;
  request.render_style = StreamSetsRequest::HIGHEST_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("data"));
  dockyard_.GetStreamSets(
      std::move(request),
      [](const StreamSetsRequest& request, const StreamSetsResponse& response) {
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(request.sample_count, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(500000ULL, response.data_sets[0][1]);
        // The Dockyard will return a level slope rather than a negative slope.
        // The result on the next line would be a negative value if negative
        // slopes were reported.
        EXPECT_EQ(0ULL, response.data_sets[0][2]);
        EXPECT_EQ(500000ULL, response.data_sets[0][3]);
        EXPECT_EQ(100000ULL, response.data_sets[0][4]);
        EXPECT_EQ(900000ULL, response.data_sets[0][5]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, RecentDataSetsCpus12) {
  constexpr uint64_t SAMPLE_COUNT = 1;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = ULLONG_MAX;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::RECENT;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu1"));
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu2"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        EXPECT_EQ(3ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(2UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[1].size());
        // Check the samples themselves.
        // CPU 1.
        EXPECT_EQ(50ULL, response.data_sets[0][0]);
        // CPU 2.
        EXPECT_EQ(50ULL, response.data_sets[1][0]);
      });

  // Kick a process call.
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, DockyardStringToId) {
  DockyardId apple_id = dockyard_.GetDockyardId("apple");
  DockyardId banana_id = dockyard_.GetDockyardId("banana");
  DockyardId carrot_id = dockyard_.GetDockyardId("carrot");
  DockyardId dog_id = dockyard_.GetDockyardId("dog");
  DockyardId elephant_id = dockyard_.GetDockyardId("elephant");
  // None of the IDs should match
  EXPECT_NE(apple_id, banana_id);
  EXPECT_NE(apple_id, carrot_id);
  EXPECT_NE(apple_id, dog_id);
  EXPECT_NE(apple_id, elephant_id);
  EXPECT_NE(banana_id, carrot_id);
  EXPECT_NE(banana_id, dog_id);
  EXPECT_NE(banana_id, elephant_id);
  EXPECT_NE(carrot_id, dog_id);
  EXPECT_NE(carrot_id, elephant_id);
  EXPECT_NE(dog_id, elephant_id);
  // The same ID should be returned for an equivalent string.
  EXPECT_EQ(apple_id, dockyard_.GetDockyardId("apple"));
  EXPECT_EQ(banana_id, dockyard_.GetDockyardId("banana"));
  EXPECT_EQ(carrot_id, dockyard_.GetDockyardId("carrot"));
  EXPECT_EQ(dog_id, dockyard_.GetDockyardId("dog"));
  EXPECT_EQ(elephant_id, dockyard_.GetDockyardId("elephant"));
}

TEST_F(SystemMonitorDockyardTest, DockyardIdToString) {
  DockyardId apple_id = dockyard_.GetDockyardId("apple");
  DockyardId banana_id = dockyard_.GetDockyardId("banana");
  DockyardId carrot_id = dockyard_.GetDockyardId("carrot");
  DockyardId dog_id = dockyard_.GetDockyardId("dog");
  DockyardId elephant_id = dockyard_.GetDockyardId("elephant");
  // Check that the id will give the corresponding string.
  std::string test;
  EXPECT_TRUE(dockyard_.GetDockyardPath(apple_id, &test));
  EXPECT_EQ("apple", test);
  EXPECT_TRUE(dockyard_.GetDockyardPath(banana_id, &test));
  EXPECT_EQ("banana", test);
  EXPECT_TRUE(dockyard_.GetDockyardPath(carrot_id, &test));
  EXPECT_EQ("carrot", test);
  EXPECT_TRUE(dockyard_.GetDockyardPath(dog_id, &test));
  EXPECT_EQ("dog", test);
  EXPECT_TRUE(dockyard_.GetDockyardPath(elephant_id, &test));
  EXPECT_EQ("elephant", test);
}

TEST_F(SystemMonitorDockyardTest, ServerListening) {
  // Test for: https://bugs.chromium.org/p/fuchsia/issues/detail?id=72
  const std::string device_name = "apple.banana.carrot.dog";

  ConnectionRequest request;
  request.SetDeviceName(device_name);
  EXPECT_TRUE(dockyard_.StartCollectingFrom(
      std::move(request), [](const dockyard::ConnectionRequest& request,
                             const dockyard::ConnectionResponse& response) {
        EXPECT_EQ(request.RequestId(), response.RequestId());
        EXPECT_EQ(request.GetMessageType(),
                  dockyard::MessageType::kConnectionRequest);
        EXPECT_EQ(response.GetMessageType(),
                  dockyard::MessageType::kResponseOk);
        EXPECT_EQ(response.DockyardVersion(), response.HarvesterVersion());
      },
      kgRPCServerAddressForTest));

  ConnectionRequest second_request;
  second_request.SetDeviceName(device_name);
  EXPECT_FALSE(dockyard_.StartCollectingFrom(
      std::move(second_request),
      [](const dockyard::ConnectionRequest& request,
         const dockyard::ConnectionResponse& response) {
        FAIL() << "Unexpected callback";
      },
      kgRPCServerAddressForTest));
  dockyard_.OnConnection(dockyard::MessageType::kResponseOk,
                         /*harvester_version=*/dockyard::DOCKYARD_VERSION);
  EXPECT_TRUE(IsGrpcServerActive());
  dockyard_.StopCollectingFromDevice();
  EXPECT_FALSE(IsGrpcServerActive());
}

TEST_F(SystemMonitorDockyardTest, VersionMismatch) {
  const std::string device_name = "apple.banana.carrot.dog";

  ConnectionRequest request;
  request.SetDeviceName(device_name);
  EXPECT_TRUE(dockyard_.StartCollectingFrom(
      std::move(request), [](const dockyard::ConnectionRequest& request,
                             const dockyard::ConnectionResponse& response) {
        EXPECT_EQ(request.RequestId(), response.RequestId());
        EXPECT_EQ(request.GetMessageType(),
                  dockyard::MessageType::kConnectionRequest);
        EXPECT_EQ(response.GetMessageType(),
                  dockyard::MessageType::kVersionMismatch);
        EXPECT_NE(response.DockyardVersion(), response.HarvesterVersion());
      },
      kgRPCServerAddressForTest));
  dockyard_.OnConnection(dockyard::MessageType::kVersionMismatch,
                         /*harvester_version=*/dockyard::DOCKYARD_VERSION - 1);
}

TEST_F(SystemMonitorDockyardTest, StreamRef) {
  SampleStreamMap stream_map;
  EXPECT_EQ(stream_map.size(), 0ULL);
  {
    SampleStream& ref1 = stream_map.StreamRef(11);
    SampleStream& ref2 = stream_map.StreamRef(22);
    // The streams should be empty.
    EXPECT_TRUE(ref1.empty());
    EXPECT_TRUE(ref2.empty());
    ref1.emplace(0, 100ULL);
    ref2.emplace(0, 200ULL);
  }
  // Requesting the stream ref caused the stream to be created.
  EXPECT_EQ(stream_map.size(), 2ULL);
  {
    SampleStream& ref1 = stream_map.StreamRef(11);
    SampleStream& ref2 = stream_map.StreamRef(22);
    // The streams should not be empty, they should have the values above.
    EXPECT_FALSE(ref1.empty());
    EXPECT_FALSE(ref2.empty());
    EXPECT_EQ(ref1[0], 100ULL);
    EXPECT_EQ(ref2[0], 200ULL);
  }
  // Requesting the same streams reuse the existing streams.
  EXPECT_EQ(stream_map.size(), 2ULL);
}

TEST_F(SystemMonitorDockyardTest, DefaultValues) {
  RandomSampleGenerator gen;
  EXPECT_EQ(0u, gen.dockyard_id);
  EXPECT_EQ(0u, gen.seed);
  EXPECT_EQ(0u, gen.start);
  EXPECT_EQ(100U, gen.finish);
  EXPECT_EQ(RandomSampleGenerator::TIME_STYLE_LINEAR, gen.time_style);
  EXPECT_EQ(RandomSampleGenerator::VALUE_STYLE_SINE_WAVE, gen.value_style);
  EXPECT_EQ(0u, gen.value_min);
  EXPECT_EQ(dockyard::SAMPLE_MAX_VALUE, gen.value_max);
  EXPECT_EQ(100u, gen.sample_count);

  DiscardSamplesRequest discard;
  EXPECT_EQ(0u, discard.start_time_ns);
  EXPECT_EQ(dockyard::kSampleTimeInfinite, discard.end_time_ns);
  EXPECT_EQ(0u, discard.dockyard_ids.size());

  StreamSetsRequest request;
  EXPECT_EQ(0u, request.start_time_ns);
  EXPECT_EQ(0u, request.end_time_ns);
  EXPECT_EQ(0u, request.sample_count);
  EXPECT_EQ(StreamSetsRequest::AVERAGE_PER_COLUMN, request.render_style);
  EXPECT_EQ(0u, request.dockyard_ids.size());
}

TEST_F(SystemMonitorDockyardTest, IgnoreSamples) {
  {
    constexpr uint64_t SAMPLE_COUNT = 40;
    RandomSampleGenerator gen;
    gen.dockyard_id = dockyard_.GetDockyardId("fake0:345:iTest");
    gen.seed = 1234;
    gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
    gen.start = 100;
    gen.finish = 500;
    gen.value_style = RandomSampleGenerator::VALUE_STYLE_SINE_WAVE;
    gen.value_min = 10;
    gen.value_max = 100;
    gen.sample_count = SAMPLE_COUNT;
    GenerateRandomSamples(gen, &dockyard_);
  }

  // Discard some samples.
  IgnoreSamplesRequest ignore;
  ignore.prefix = "fake0:";
  ignore.suffix = ":iTest";
  dockyard_.IgnoreSamples(
      std::move(ignore), [](const dockyard::IgnoreSamplesRequest& request,
                            const dockyard::IgnoreSamplesResponse& response) {
        EXPECT_EQ(request.RequestId(), response.RequestId());
      });
  dockyard_.ProcessRequests();

  {
    constexpr uint64_t SAMPLE_COUNT = 40;
    RandomSampleGenerator gen;
    gen.dockyard_id = dockyard_.GetDockyardId("fake0:345:iTest");
    gen.seed = 1234;
    gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
    gen.start = 500;
    gen.finish = 700;
    gen.value_style = RandomSampleGenerator::VALUE_STYLE_SINE_WAVE;
    gen.value_min = 10;
    gen.value_max = 100;
    gen.sample_count = SAMPLE_COUNT;
    GenerateRandomSamples(gen, &dockyard_);
  }

  // Add pending request that straddles the time when those samples were not
  // ignored and then some time when they were ignored.
  StreamSetsRequest request;
  request.start_time_ns = 400;
  request.end_time_ns = 600;
  request.sample_count = 12;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0:345:iTest"));
  dockyard_.GetStreamSets(
      std::move(request),
      [](const StreamSetsRequest& request, const StreamSetsResponse& response) {
        // Check results.
        EXPECT_EQ(request.RequestId(), response.RequestId());
        EXPECT_EQ(10ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        // Check the samples themselves. The samples that arrived prior to the
        // request to ignore some samples will still be there. Samples that
        // arrived (i.e. were generated) after the request to ignore them are
        // ignored.
        EXPECT_EQ(41u, response.data_sets[0][0]);
        EXPECT_EQ(58u, response.data_sets[0][1]);
        EXPECT_EQ(72u, response.data_sets[0][2]);
        EXPECT_EQ(83u, response.data_sets[0][3]);
        EXPECT_EQ(94u, response.data_sets[0][4]);
        EXPECT_EQ(99u, response.data_sets[0][5]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][6]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][7]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][8]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][10]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][11]);
      });
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, Discard) {
  constexpr uint64_t SAMPLE_COUNT = 40;
  RandomSampleGenerator gen;
  gen.dockyard_id = dockyard_.GetDockyardId("fake0");
  gen.seed = 1234;
  gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
  gen.start = 100;
  gen.finish = 500;
  gen.value_style = RandomSampleGenerator::VALUE_STYLE_SINE_WAVE;
  gen.value_min = 10;
  gen.value_max = 100;
  gen.sample_count = SAMPLE_COUNT;
  GenerateRandomSamples(gen, &dockyard_);

  // Discard some samples.
  DiscardSamplesRequest discard;
  discard.start_time_ns = 0;
  discard.end_time_ns = 300;
  discard.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0"));
  dockyard_.DiscardSamples(
      std::move(discard), [](const DiscardSamplesRequest& request,
                             const DiscardSamplesResponse& response) {
        EXPECT_EQ(request.RequestId(), response.RequestId());
      });
  dockyard_.ProcessRequests();

  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::RECENT;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("fake0"));
  dockyard_.GetStreamSets(
      std::move(request), [SAMPLE_COUNT](const StreamSetsRequest& request,
                                         const StreamSetsResponse& response) {
        // Check results.
        EXPECT_EQ(request.RequestId(), response.RequestId());
        EXPECT_EQ(10ULL, response.lowest_value);
        EXPECT_EQ(100ULL, response.highest_value);
        ASSERT_EQ(1UL, response.data_sets.size());
        ASSERT_EQ(SAMPLE_COUNT, response.data_sets[0].size());
        // Check the samples themselves.
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][0]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][9]);
        EXPECT_EQ(dockyard::NO_DATA, response.data_sets[0][19]);
        EXPECT_EQ(29ULL, response.data_sets[0][29]);
        EXPECT_EQ(99ULL, response.data_sets[0][39]);
      });
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, MessageTypes) {
#define TEST_MESSAGE_TYPES(x)                                             \
  do {                                                                    \
    dockyard::x##Request r1;                                              \
    EXPECT_EQ(r1.GetMessageType(), dockyard::MessageType::k##x##Request); \
  } while (false)

  TEST_MESSAGE_TYPES(Connection);
  TEST_MESSAGE_TYPES(DiscardSamples);
  TEST_MESSAGE_TYPES(IgnoreSamples);
  TEST_MESSAGE_TYPES(StreamSets);
  TEST_MESSAGE_TYPES(UnignoreSamples);

#undef TEST_MESSAGE_TYPES
}

}  // namespace
}  // namespace dockyard
