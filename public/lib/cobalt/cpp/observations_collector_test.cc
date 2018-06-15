// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/observations_collector.h"

#include "gtest/gtest.h"

const int64_t kPeriodSize = 1000;
const int64_t kPeriodCount = 1000;
const int64_t kThreadNum = 100;
const int64_t kSamplerMaxInt = 20;

namespace cobalt {

namespace {
// Function that increments a counter kPeriodSize * kPeriodCount times in
// kPeriodCount increments with some random jitter in between.
void DoIncrement(std::shared_ptr<Counter> counter) {
  for (int64_t i = 0; i < kPeriodCount; i++) {
    for (int64_t j = 0; j < kPeriodSize; j++) {
      counter->Increment();
    }
    // Introduce jitter to test.
    std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 100));
  }
}

// Sink is used to gather all the observations sent by the MetricFactory.
struct Sink {
  explicit Sink(bool with_errors) : with_errors(with_errors) {}

  std::vector<size_t> SendObservations(const std::vector<Observation>* obs) {
    std::vector<size_t> errors;

    for (auto iter = std::make_move_iterator(obs->begin());
         std::make_move_iterator(obs->end()) != iter; iter++) {
      // Randomly fail to "send" some observations.
      if (with_errors && std::rand() % 5 == 0) {
        errors.push_back(
            std::distance(std::make_move_iterator(obs->begin()), iter));
        continue;
      }
      observations.push_back(*iter);
    }
    return errors;
  }

  std::vector<Observation> observations;
  bool with_errors;
};

}  // namespace

// Checks that Counters work correctly with many threads updating them.
TEST(Counter, Normal) {
  // Metric id.
  const int64_t kMetricId = 10;
  Sink sink(true);
  ObservationsCollector collector(
      std::bind(&Sink::SendObservations, &sink, std::placeholders::_1), 1);
  auto counter = collector.MakeCounter(kMetricId, "part_name");

  // Each thread will add kPeriodSize * kPeriodCount to the counter.
  int64_t expected = kPeriodSize * kPeriodCount * kThreadNum;
  std::vector<std::thread> threads;

  // Start all the incrementer threads.
  for (int i = 0; i < kThreadNum; i++) {
    threads.push_back(std::thread(DoIncrement, counter));
  }

  // Start the collection thread.
  collector.Start(std::chrono::microseconds(10));

  // Wait until all the incrementer threads have finished.
  for (auto iter = threads.begin(); iter != threads.end(); iter++) {
    iter->join();
  }

  // Stop the collection thread.
  collector.Stop();

  // Add up all the observations in the sink.
  int64_t actual = 0;
  for (auto iter = sink.observations.begin(); sink.observations.end() != iter;
       iter++) {
    actual += (*iter).parts[0].value.GetIntValue();
  }

  EXPECT_EQ(expected, actual);
}

// Function that logs kPeriodSize * kPeriodCount random integers to an
// IntegerSampler in kPeriodCount increments with some random jitter in between.
void DoLogObservation(std::shared_ptr<IntegerSampler> int_sampler) {
  std::random_device rd;
  std::default_random_engine gen(rd());
  // Uniformly sample from the set [0...kSamplerMaxInt].
  std::uniform_int_distribution<> dis(0, kSamplerMaxInt);

  for (int64_t i = 0; i < kPeriodCount; i++) {
    for (int64_t j = 0; j < kPeriodSize; j++) {
      int_sampler->LogObservation(dis(gen));
    }
    // Introduce jitter to test.
    std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 100));
  }
}

// Test that the average of the samples is within an expected range of the
// theoretical average.
TEST(IntegerSampler, IntegerAverage) {
  // Metric id.
  const int64_t kMetricId = 10;
  Sink sink(false);
  ObservationsCollector collector(
      std::bind(&Sink::SendObservations, &sink, std::placeholders::_1), 1);

  // The IntegerSampler will collect at most 100 elements at a time.
  size_t sample_size = 100;
  auto int_sampler =
      collector.MakeIntegerSampler(kMetricId, "part_name", sample_size);

  // Each thread will log kPeriodSize * kPeriodCount times to the
  // IntegerSampler.
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreadNum; i++) {
    threads.push_back(std::thread(DoLogObservation, int_sampler));
  }

  // Start the collection thread.
  collector.Start(std::chrono::microseconds(10));

  // Wait until all the logging threads have finished.
  for (auto iter = threads.begin(); iter != threads.end(); iter++) {
    iter->join();
  }

  // Stop the collection thread.
  collector.Stop();

  // In the worst case scenario, the number of collected observations is
  // kPeriodSize * kPeriodCount * kThreadNum.
  // In the worst case scenario, where every generated number is 20, the total
  // is 2*10^9. This comfortably fits in a 64 bits signed integer.
  int64_t total = 0;
  int64_t num_obs = 0;
  for (auto iter = sink.observations.begin(); sink.observations.end() != iter;
       iter++) {
    num_obs++;
    total += (*iter).parts[0].value.GetIntValue();
  }
  double sample_mean =
      static_cast<double>(total) / static_cast<double>(num_obs);

  // We sample num_obs elements from the uniform distribution
  // [0...kSamplerMaxInt]. The central limit theorem tells us the average of
  // these num_obs samples will be normally distributed with the same mean as
  // the uniform distribution and a variance equal to 1/num_obs times the
  // uniform distribution's variance.
  double expected_mean = kSamplerMaxInt / 2;
  double expected_stddev =
      std::sqrt((kSamplerMaxInt + 1) * (kSamplerMaxInt) / 12.0 / num_obs);
  // We test to see if the sample mean is within 4.5 standard deviations of
  // the expected mean. This should prevent false positives at least 99.999% of
  // the time.
  EXPECT_NEAR(expected_mean, sample_mean, expected_stddev * 4.5);
}

// Test that if a source of data has a very strong time-dependent bias, the
// IntegerSampler does not reflect that time-dependency.
TEST(IntegerSampler, CheckUniformity) {
  // Metric id.
  const int64_t kMetricId = 10;
  Sink sink(false);
  ObservationsCollector collector(
      std::bind(&Sink::SendObservations, &sink, std::placeholders::_1), 1);

  // The IntegerSampler will collect at most 100 elements at a time.
  size_t sample_size = 100;
  auto int_sampler =
      collector.MakeIntegerSampler(kMetricId, "part_name", sample_size);

  // We log integers in increasing order. This is to check that the ordering of
  // the logged observations is not related to the distribution of the sampled
  // observations.
  for (int64_t val = 0; val <= kSamplerMaxInt; val++) {
    for (size_t i = 0; i < sample_size * 10; i++) {
      int_sampler->LogObservation(val);
    }
  }

  collector.CollectAll();

  int64_t total = 0;
  int64_t num_obs = 0;
  for (auto iter = sink.observations.begin(); sink.observations.end() != iter;
       iter++) {
    num_obs++;
    total += (*iter).parts[0].value.GetIntValue();
  }
  double sample_mean =
      static_cast<double>(total) / static_cast<double>(num_obs);

  // We sample num_obs elements from the uniform distribution
  // [0...kSamplerMaxInt]. The central limit theorem tells us the average of
  // these num_obs samples will be normally distributed with the same mean as
  // the uniform distribution and a variance equal to 1/num_obs times the
  // uniform distribution's variance.
  double expected_mean = kSamplerMaxInt / 2;
  double expected_stddev =
      std::sqrt((kSamplerMaxInt + 1) * (kSamplerMaxInt) / 12.0 / num_obs);
  // We test to see if the sample mean is within 4.5 standard deviations of
  // the expected mean. This should prevent false positives at least 99.999% of
  // the time.
  EXPECT_NEAR(expected_mean, sample_mean, expected_stddev * 4.5);
}

void DoLogEvent(std::shared_ptr<EventLogger> logger, uint32_t status) {
  std::random_device rd;
  std::default_random_engine gen(rd());
  // Uniformly sample from the set [0...kSamplerMaxInt].
  std::uniform_int_distribution<> time(0, kSamplerMaxInt);

  for (int64_t i = 0; i < kPeriodCount; i++) {
    for (int64_t j = 0; j < kPeriodSize; j++) {
      logger->LogEvent(time(gen), status);
    }
    // Introduce jitter to test.
    std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 100));
  }
}

TEST(EventLogger, Normal) {
  const uint32_t kEventMetricId = 1;
  const uint32_t kMaxStatus = 4;
  const uint32_t kEventTimingMetricId = 2;
  const size_t kThreadNum = 100;
  ASSERT_EQ(kThreadNum % (kMaxStatus + 1), size_t(0))
      << "kThreadNum must be divisible by the number of statuses.";
  size_t samples = 100;
  Sink sink(false);
  ObservationsCollector collector(
      std::bind(&Sink::SendObservations, &sink, std::placeholders::_1), 1);
  auto logger = collector.MakeEventLogger(kEventMetricId, kMaxStatus,
                                          kEventTimingMetricId, samples);

  std::vector<std::thread> threads;
  for (size_t i = 0; i < kThreadNum; i++) {
    uint32_t status = i % (kMaxStatus + 1);
    threads.push_back(std::thread(DoLogEvent, logger, status));
  }

  // Start the collection thread.
  collector.Start(std::chrono::microseconds(10));

  // Wait until all the logging threads have finished.
  for (auto iter = threads.begin(); iter != threads.end(); iter++) {
    iter->join();
  }

  // Stop the collection thread.
  collector.Stop();

  // We sum up the distributions and check the sum is as expected.
  int64_t histogram[kMaxStatus + 1] = {0, 0, 0, 0, 0};
  for (auto iter = sink.observations.begin(); sink.observations.end() != iter;
       iter++) {
    if (iter->metric_id != kEventMetricId) continue;

    for (auto part_iter = iter->parts.begin(); iter->parts.end() != part_iter;
         part_iter++) {
      if (part_iter->part_name == "status") {
        auto dist = part_iter->value.GetDistribution();
        for (auto status_iter = dist.begin(); dist.end() != status_iter;
             status_iter++) {
          histogram[status_iter->first] += status_iter->second;
        }
      }
    }
  }

  int64_t expected_histogram_value =
      kThreadNum / (kMaxStatus + 1) * kPeriodSize * kPeriodCount;
  for (size_t status = 0; status <= kMaxStatus; status++) {
    EXPECT_EQ(histogram[status], expected_histogram_value);
  }
}

// Check that the integer value part work correctly.
TEST(ValuePart, IntValuePart) {
  ValuePart value = ValuePart::MakeInt(10);
  EXPECT_EQ(10, value.GetIntValue());
  EXPECT_TRUE(value.IsIntValue());
  EXPECT_EQ(ValuePart::INT, value.Which());
}

TEST(ValuePart, DoubleValuePart) {
  ValuePart value = ValuePart::MakeDouble(10.5);
  EXPECT_DOUBLE_EQ(10.5, value.GetDoubleValue());
  EXPECT_TRUE(value.IsDoubleValue());
  EXPECT_EQ(ValuePart::DOUBLE, value.Which());
}

TEST(ValuePart, DistributionValuePart) {
  std::map<uint32_t, int64_t> distribution = {{0, 1}, {1, 2}, {2, 4}};
  ValuePart value = ValuePart::MakeDistribution(distribution);
  EXPECT_EQ(distribution.size(), value.GetDistribution().size());
  EXPECT_TRUE(value.IsDistribution());
  EXPECT_EQ(ValuePart::DISTRIBUTION, value.Which());

  // Check that the copy constructor works.
  ValuePart copy = value;
  EXPECT_EQ(distribution.size(), copy.GetDistribution().size());
  EXPECT_TRUE(copy.IsDistribution());
  EXPECT_EQ(ValuePart::DISTRIBUTION, copy.Which());
}
}  // namespace cobalt
