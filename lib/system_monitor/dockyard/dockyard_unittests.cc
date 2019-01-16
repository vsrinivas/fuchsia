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
    _name_call_count = 100;  // Arbitrary.
    _sets_call_count = 200;  // Arbitrary.
    EXPECT_EQ(nullptr,
              _dockyard.SetStreamNamesHandler(std::bind(
                  &SystemMonitorDockyardTest::TestStreamNamesCallback, this,
                  std::placeholders::_1, std::placeholders::_2)));
    EXPECT_EQ(nullptr, _dockyard.SetStreamSetsHandler(std::bind(
                           &SystemMonitorDockyardTest::TestStreamSetsCallback,
                           this, std::placeholders::_1)));
    // Add some samples.
    _dockyard.AddSamples(_dockyard.GetSampleStreamId("cpu0"),
                         {{10ULL, 8ULL}, {200ULL, 10ULL}, {300ULL, 20ULL}});
    _dockyard.AddSamples(_dockyard.GetSampleStreamId("cpu1"),
                         {
                             {10ULL, 3ULL},
                             {20ULL, 4ULL},
                             {80ULL, 5ULL},
                             {81ULL, 50ULL},
                             {100ULL, 10ULL},
                             {200ULL, 100ULL},
                             {300ULL, 80ULL},
                             {400ULL, 100ULL},
                             {500ULL, 50ULL},
                         });
    _dockyard.AddSamples(
        _dockyard.GetSampleStreamId("cpu2"),
        {
            {100ULL, 3ULL},  {105ULL, 4ULL},   {110ULL, 5ULL},  {115ULL, 50ULL},
            {120ULL, 90ULL}, {125ULL, 100ULL}, {130ULL, 80ULL}, {135ULL, 45ULL},
            {140ULL, 44ULL}, {150ULL, 40ULL},  {155ULL, 30ULL}, {160ULL, 12ULL},
            {165ULL, 10ULL}, {170ULL, 8ULL},   {175ULL, 5ULL},  {180ULL, 3ULL},
            {185ULL, 5ULL},  {190ULL, 15ULL},  {195ULL, 50ULL},
        });
  }

  void TestStreamNamesCallback(const std::vector<StreamInfo>& add,
                               const std::vector<uint32_t>& remove) {
    ++_name_call_count;
  }

  void TestStreamSetsCallback(const StreamSetsResponse& response) {
    ++_sets_call_count;
    _response = response;
  }

  int32_t _name_call_count;
  int32_t _sets_call_count;
  Dockyard _dockyard;
  StreamSetsResponse _response;
};

TEST_F(SystemMonitorDockyardTest, NameCallback) {
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(200, _sets_call_count);
  _dockyard.ProcessRequests();
}

TEST_F(SystemMonitorDockyardTest, SetsCallback) {
  // No pending requests.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(200, _sets_call_count);
}

TEST_F(SystemMonitorDockyardTest, RawPastEndResponse) {
  constexpr uint64_t SAMPLE_COUNT = 10;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 1000;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu0"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(8ULL, _response.lowest_value);
  EXPECT_EQ(20ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(8ULL, _response.data_sets[0][0]);
  EXPECT_EQ(10ULL, _response.data_sets[0][1]);
  EXPECT_EQ(15ULL, _response.data_sets[0][2]);
  EXPECT_EQ(20ULL, _response.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][5]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][7]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][9]);
}

TEST_F(SystemMonitorDockyardTest, RawSparseResponse) {
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 0;
  request.end_time_ns = 300;
  request.sample_count = 10;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu0"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(8ULL, _response.lowest_value);
  EXPECT_EQ(20ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(10UL, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(8ULL, _response.data_sets[0][0]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][1]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][2]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][5]);
  EXPECT_EQ(10ULL, _response.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][7]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][8]);
  EXPECT_EQ(20ULL, _response.data_sets[0][9]);
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpu1) {
  constexpr uint64_t SAMPLE_COUNT = 10;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 50;
  request.end_time_ns = 450;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu1"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(27ULL, _response.data_sets[0][0]);
  EXPECT_EQ(10ULL, _response.data_sets[0][1]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][2]);
  EXPECT_EQ(100ULL, _response.data_sets[0][3]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][4]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][5]);
  EXPECT_EQ(80ULL, _response.data_sets[0][6]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][7]);
  EXPECT_EQ(100ULL, _response.data_sets[0][8]);
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][9]);
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(30ULL, _response.data_sets[0][0]);
  EXPECT_EQ(71ULL, _response.data_sets[0][1]);
  EXPECT_EQ(31ULL, _response.data_sets[0][2]);
  EXPECT_EQ(7ULL, _response.data_sets[0][3]);
  EXPECT_EQ(18ULL, _response.data_sets[0][4]);
}

TEST_F(SystemMonitorDockyardTest, RawDataSetsCpus012) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu0"));
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu1"));
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(3UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[1].size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[2].size());
  // Check the samples themselves.
  // CPU 0.
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[0][0]);
  EXPECT_EQ(10ULL, _response.data_sets[0][1]);
  // CPU 1.
  EXPECT_EQ(10ULL, _response.data_sets[1][0]);
  EXPECT_EQ(100ULL, _response.data_sets[1][1]);
  // CPU 2.
  EXPECT_EQ(46ULL, _response.data_sets[2][0]);
  EXPECT_EQ(17ULL, _response.data_sets[2][1]);
}

TEST_F(SystemMonitorDockyardTest, HighDataSetsCpus12) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 20;
  request.end_time_ns = 150;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::HIGHEST_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu1"));
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(2UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  // CPU 1.
  EXPECT_EQ(50ULL, _response.data_sets[0][0]);
  EXPECT_EQ(10ULL, _response.data_sets[0][1]);
  // CPU 2.
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[1][0]);
  EXPECT_EQ(100ULL, _response.data_sets[1][1]);
}

TEST_F(SystemMonitorDockyardTest, LowDataSetsCpus12) {
  constexpr uint64_t SAMPLE_COUNT = 2;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 20;
  request.end_time_ns = 150;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::LOWEST_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu1"));
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(2UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  // CPU 1.
  EXPECT_EQ(4ULL, _response.data_sets[0][0]);
  EXPECT_EQ(10ULL, _response.data_sets[0][1]);
  // CPU 2.
  EXPECT_EQ(dockyard::NO_DATA, _response.data_sets[1][0]);
  EXPECT_EQ(3ULL, _response.data_sets[1][1]);
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
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(278350ULL, _response.data_sets[0][0]);
  EXPECT_EQ(701030ULL, _response.data_sets[0][1]);
  EXPECT_EQ(288659ULL, _response.data_sets[0][2]);
  EXPECT_EQ(41237ULL, _response.data_sets[0][3]);
  EXPECT_EQ(154639ULL, _response.data_sets[0][4]);
}

TEST_F(SystemMonitorDockyardTest, SmoothDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::WIDE_SMOOTHING;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(46ULL, _response.data_sets[0][0]);
  EXPECT_EQ(41ULL, _response.data_sets[0][1]);
  EXPECT_EQ(38ULL, _response.data_sets[0][2]);
  EXPECT_EQ(20ULL, _response.data_sets[0][3]);
  EXPECT_EQ(13ULL, _response.data_sets[0][4]);
}

TEST_F(SystemMonitorDockyardTest, SculptedDataSetsCpu2) {
  constexpr uint64_t SAMPLE_COUNT = 5;
  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 200;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::SCULPTING;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("cpu2"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(3ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(3ULL, _response.data_sets[0][0]);
  EXPECT_EQ(100ULL, _response.data_sets[0][1]);
  EXPECT_EQ(12ULL, _response.data_sets[0][2]);
  EXPECT_EQ(3ULL, _response.data_sets[0][3]);
  EXPECT_EQ(3ULL, _response.data_sets[0][4]);
}

TEST_F(SystemMonitorDockyardTest, RandomSamples) {
  constexpr uint64_t SAMPLE_COUNT = 40;
  RandomSampleGenerator gen;
  gen.stream_id = _dockyard.GetSampleStreamId("fake0");
  gen.seed = 1234;
  gen.time_style = RandomSampleGenerator::TIME_STYLE_LINEAR;
  gen.start = 100;
  gen.finish = 500;
  gen.value_style = RandomSampleGenerator::VALUE_STYLE_SINE_WAVE;
  gen.value_min = 10;
  gen.value_max = 100;
  gen.sample_count = SAMPLE_COUNT;
  GenerateRandomSamples(gen, &_dockyard);

  // Add pending request.
  StreamSetsRequest request;
  request.start_time_ns = 100;
  request.end_time_ns = 500;
  request.sample_count = SAMPLE_COUNT;
  request.render_style = StreamSetsRequest::AVERAGE_PER_COLUMN;
  request.stream_ids.push_back(_dockyard.GetSampleStreamId("fake0"));
  _dockyard.GetStreamSets(&request);

  // Kick a process call.
  _dockyard.ProcessRequests();
  EXPECT_EQ(100, _name_call_count);
  EXPECT_EQ(201, _sets_call_count);
  EXPECT_EQ(10ULL, _response.lowest_value);
  EXPECT_EQ(100ULL, _response.highest_value);
  ASSERT_EQ(1UL, _response.data_sets.size());
  ASSERT_EQ(SAMPLE_COUNT, _response.data_sets[0].size());
  // Check the samples themselves.
  EXPECT_EQ(59ULL, _response.data_sets[0][0]);
  EXPECT_EQ(97ULL, _response.data_sets[0][9]);
  EXPECT_EQ(26ULL, _response.data_sets[0][19]);
  EXPECT_EQ(33ULL, _response.data_sets[0][29]);
  EXPECT_EQ(99ULL, _response.data_sets[0][39]);
}

}  // namespace
}  // namespace dockyard
