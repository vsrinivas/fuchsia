// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "garnet/lib/system_monitor/dockyard/test_sample_generator.h"
#include "gtest/gtest.h"

namespace dockyard {
namespace {

class SystemMonitorDockyardTest : public ::testing::Test {
 public:
  void SetUp() {
    // Initialize to distinct values for testing.
    name_call_count_ = 100;  // Arbitrary.
    sets_call_count_ = 200;  // Arbitrary.
    EXPECT_EQ(nullptr, dockyard_.SetDockyardPathsHandler(std::bind(
                           &SystemMonitorDockyardTest::TestPathsCallback, this,
                           std::placeholders::_1, std::placeholders::_2)));
    EXPECT_EQ(nullptr, dockyard_.SetStreamSetsHandler(std::bind(
                           &SystemMonitorDockyardTest::TestStreamSetsCallback,
                           this, std::placeholders::_1)));
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

  void TestPathsCallback(const std::vector<PathInfo>& add,
                         const std::vector<uint32_t>& remove) {
    ++name_call_count_;
  }

  void TestStreamSetsCallback(const StreamSetsResponse& response) {
    ++sets_call_count_;
    response_ = response;
  }

  int32_t name_call_count_;
  int32_t sets_call_count_;
  Dockyard dockyard_;
  StreamSetsResponse response_;
};

TEST_F(SystemMonitorDockyardTest, NameCallback) {
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(200, sets_call_count_);
  dockyard_.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SetsCallback) {
  // No pending requests.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(200, sets_call_count_);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);

  EXPECT_EQ(0ULL, response_.lowest_value);
  EXPECT_EQ(dockyard::SLOPE_LIMIT, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(500000ULL, response_.data_sets[0][0]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][1]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][2]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][3]);
  EXPECT_EQ(225000ULL, response_.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][5]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);

  EXPECT_EQ(0ULL, response_.lowest_value);
  EXPECT_EQ(1000000ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(20600000ULL, response_.data_sets[0][0]);
  EXPECT_EQ(200000ULL, response_.data_sets[0][1]);
  EXPECT_EQ(600000ULL, response_.data_sets[0][2]);
  EXPECT_EQ(1000000ULL, response_.data_sets[0][3]);
  EXPECT_EQ(0ULL, response_.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][5]);
  EXPECT_EQ(1000000ULL, response_.data_sets[0][6]);
  EXPECT_EQ(1000000ULL, response_.data_sets[0][7]);
  EXPECT_EQ(600000ULL, response_.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][9]);
  EXPECT_EQ(200000ULL, response_.data_sets[0][10]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][11]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][12]);
  EXPECT_EQ(0ULL, response_.data_sets[0][13]);
  EXPECT_EQ(200000ULL, response_.data_sets[0][14]);
  EXPECT_EQ(400000ULL, response_.data_sets[0][15]);
  EXPECT_EQ(600000ULL, response_.data_sets[0][16]);
  EXPECT_EQ(800000ULL, response_.data_sets[0][17]);
  EXPECT_EQ(1000000ULL, response_.data_sets[0][18]);
  EXPECT_EQ(200000ULL, response_.data_sets[0][19]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);

  EXPECT_EQ(0ULL, response_.lowest_value);
  EXPECT_EQ(1000000ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(7428571ULL, response_.data_sets[0][0]);
  EXPECT_EQ(571428ULL, response_.data_sets[0][1]);
  EXPECT_EQ(1000000ULL, response_.data_sets[0][2]);
  EXPECT_EQ(428571ULL, response_.data_sets[0][3]);
  EXPECT_EQ(0ULL, response_.data_sets[0][4]);
  EXPECT_EQ(285714ULL, response_.data_sets[0][5]);
  EXPECT_EQ(642857ULL, response_.data_sets[0][6]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(8ULL, response_.lowest_value);
  EXPECT_EQ(20ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(8ULL, response_.data_sets[0][0]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][1]);
  EXPECT_EQ(10ULL, response_.data_sets[0][2]);
  EXPECT_EQ(20ULL, response_.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][5]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][7]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][9]);
}

TEST_F(SystemMonitorDockyardTest, RawSparseResponse) {
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 300;
  request.sample_count = 10;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.dockyard_ids.push_back(dockyard_.GetDockyardId("cpu0"));
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(8ULL, response_.lowest_value);
  EXPECT_EQ(20ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(10UL, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(8ULL, response_.data_sets[0][0]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][1]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][2]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][5]);
  EXPECT_EQ(10ULL, response_.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][7]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][9]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(27ULL, response_.data_sets[0][0]);
  EXPECT_EQ(10ULL, response_.data_sets[0][1]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][2]);
  EXPECT_EQ(100ULL, response_.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][5]);
  EXPECT_EQ(80ULL, response_.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][7]);
  EXPECT_EQ(100ULL, response_.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][9]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(15ULL, response_.data_sets[0][0]);
  EXPECT_EQ(78ULL, response_.data_sets[0][1]);
  EXPECT_EQ(38ULL, response_.data_sets[0][2]);
  EXPECT_EQ(8ULL, response_.data_sets[0][3]);
  EXPECT_EQ(18ULL, response_.data_sets[0][4]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(3UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[1].size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[2].size());
  // Check the samples themselves.
  // CPU 0.
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][0]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[0][1]);
  // CPU 1.
  EXPECT_EQ(10ULL, response_.data_sets[1][0]);
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[1][1]);
  // CPU 2.
  EXPECT_EQ(46ULL, response_.data_sets[2][0]);
  EXPECT_EQ(17ULL, response_.data_sets[2][1]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(2UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  // CPU 1.
  EXPECT_EQ(50ULL, response_.data_sets[0][0]);
  EXPECT_EQ(10ULL, response_.data_sets[0][1]);
  // CPU 2.
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[1][0]);
  EXPECT_EQ(100ULL, response_.data_sets[1][1]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(2UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  // CPU 1.
  EXPECT_EQ(4ULL, response_.data_sets[0][0]);
  EXPECT_EQ(10ULL, response_.data_sets[0][1]);
  // CPU 2.
  EXPECT_EQ(dockyard::NO_DATA, response_.data_sets[1][0]);
  EXPECT_EQ(3ULL, response_.data_sets[1][1]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(123711ULL, response_.data_sets[0][0]);
  EXPECT_EQ(773195ULL, response_.data_sets[0][1]);
  EXPECT_EQ(360824ULL, response_.data_sets[0][2]);
  EXPECT_EQ(51546ULL, response_.data_sets[0][3]);
  EXPECT_EQ(154639ULL, response_.data_sets[0][4]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(47ULL, response_.data_sets[0][0]);
  EXPECT_EQ(44ULL, response_.data_sets[0][1]);
  EXPECT_EQ(42ULL, response_.data_sets[0][2]);
  EXPECT_EQ(20ULL, response_.data_sets[0][3]);
  EXPECT_EQ(13ULL, response_.data_sets[0][4]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(3ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(3ULL, response_.data_sets[0][0]);
  EXPECT_EQ(100ULL, response_.data_sets[0][1]);
  EXPECT_EQ(30ULL, response_.data_sets[0][2]);
  EXPECT_EQ(5ULL, response_.data_sets[0][3]);
  EXPECT_EQ(3ULL, response_.data_sets[0][4]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  EXPECT_EQ(100, name_call_count_);
  EXPECT_EQ(201, sets_call_count_);
  EXPECT_EQ(10ULL, response_.lowest_value);
  EXPECT_EQ(100ULL, response_.highest_value);
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(55ULL, response_.data_sets[0][0]);
  EXPECT_EQ(99ULL, response_.data_sets[0][9]);
  EXPECT_EQ(29ULL, response_.data_sets[0][19]);
  EXPECT_EQ(29ULL, response_.data_sets[0][29]);
  EXPECT_EQ(99ULL, response_.data_sets[0][39]);
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
  dockyard_.GetStreamSets(&request);

  // Kick a process call.
  dockyard_.ProcessRequests();
  ASSERT_EQ(1UL, response_.data_sets.size());
  ASSERT_EQ(request.sample_count, response_.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(500000ULL, response_.data_sets[0][0]);
  EXPECT_EQ(500000ULL, response_.data_sets[0][1]);
  // The Dockyard will return a level slope rather than a negative slope. The
  // result on the next line would be a negative value if negative slopes were
  // reported.
  EXPECT_EQ(0ULL, response_.data_sets[0][2]);
  EXPECT_EQ(500000ULL, response_.data_sets[0][3]);
  EXPECT_EQ(100000ULL, response_.data_sets[0][4]);
  EXPECT_EQ(900000ULL, response_.data_sets[0][5]);
}

}  // namespace
}  // namespace dockyard
