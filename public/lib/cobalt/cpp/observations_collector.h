// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a library to be used by users of Cobalt in order to
// collect metrics at a high frequency. The main building blocks are the
// ObservationsCollector, EventLogger, Counter and IntegerSampler classes.
//
// Example: counting and timing function calls
//
// ObservationsCollector collector(send_to_cobalt_function_pointer,
//                                 kDefaultEncodingId);
//
// auto foo_calls = collector.MakeEventLogger(kFooCallsMetricId,
//                                            1, // Success = 0, Failure = 1
//                                            kFooCallsTimerMetricId,
//                                            kNumberOfSamples);
//
// auto bar_calls = collector.MakeCounter(kBarCallsMetricId,
//                                        kBarCallsMetricPartName);
//
// auto bar_input_size = collector.MakeIntegerSampler(kBarInputSizeMetricId,
//                                                    kBarInputSizePartName,
//                                                    kNumberOfSamples);
//
//
// // Perform aggregation and send to Cobalt FIDL service every 1 minute.
// collector.Start(std::chrono::minutes(1));
//
// int Foo() {
//   int64_t start = getCurTime();
//   DoSomeFooWork
//   ...
//   // Logs the amount of time Foo took to execute, and what the return
//   // status was.
//   foo_calls.LogEvent(getCurTime() - start, return_status);
//   return return_status;
// }
//
// void Bar(std::vector<int> input) {
//   bar_calls.Increment();
//   // Logs the size of the input to Bar. bar_input_size is an integer
//   // sampler that will randomly select kNumberOfSamples logged
//   // observations to be sent to Cobalt.
//   bar_input_size.LogObservation(input.size());
//   DoSomeBarWork
//   ...
// }

#ifndef LIB_COBALT_CPP_OBSERVATIONS_COLLECTOR_H_
#define LIB_COBALT_CPP_OBSERVATIONS_COLLECTOR_H_

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "garnet/public/lib/cobalt/cpp/observation.h"

namespace cobalt {

// A SendObservationsFn is a callable object that takes a pointer to a vector
// of observations and returns a list of the observation indices for
// observations that failed to be sent. An empty list is returned on success.
// The expectation is that this function will send observations to a consumer
// such as sending observations to the Cobalt FIDL service on Fuchsia.
typedef std::function<std::vector<size_t>(const std::vector<Observation>*)>
    SendObservationsFn;

// A Counter allows you to keep track of the number of times an event has
// occured. A counter is associated with a metric part.
// Incrementing a counter is thread-safe.
class Counter {
 public:
  // Increments the counter by 1.
  inline void Increment() { counter_++; }

 private:
  friend class MetricObservers;

  Counter(const std::string& part_name, uint32_t encoding_id)
      : counter_(0), part_name_(part_name), encoding_id_(encoding_id) {}

  // Returns an integer ObservationPart and sets the counter's value to 0.
  // If the ObservationPart undo function is called, the counter's value is
  // added back on top of the counter.
  ObservationPart GetObservationPart();

  std::atomic<int64_t> counter_;
  std::string part_name_;
  uint32_t encoding_id_;
};

// A Sampler has an associated |size| passed as |samples| to the Make*Sampler()
// method on the ObservationsCollector.
// Each collection period, the Sampler will attempt to uniformly sample up to
// |size| of the logged observations. The sampled observations will be
// collected by the ObservationsCollector.
// LogObservation is thread-safe.
template <class T>
class Sampler {
 public:
  void LogObservation(const T& value) {
    size_t idx = num_seen_++;
    // idx should now be a unique number.

    if (idx < size_) {
      // Race Condition Note: If a thread is pre-empted here, a race condition
      // can occur.
      // t1: Thread1 calls LogObservation(10), is assigned idx=5 < size_ and is
      // pre-empted here.
      // t2: Thread2 calls LogObservation(20), is assigned idx=10 > size_ and
      // satisfies the condition gap_-- == 0. Let index = 5. So now,
      // reservoir_[5] == 20.
      // t3: Thread1 resumes and sets reservoir_[5] == 10.
      // This condition alters the sampling such that the first size_ values
      // that were logged will be slightly more likely to be sampled than ideal.
      // This is considered benign.
      reservoir_[idx] = value;
      written_[idx] = true;
      return;
    }

    // Race Condition Note: If thread1 and thread2 are racing to decrement gap_,
    // then gap_ could end up being negative. This is why we choose to check
    // gap_ == 0. In the above case, exactly one of thread1 or thread2 will see
    // gap_ == 0. Unless there is significant bias in scheduling and in the data
    // provided by thread1 and thread2, this should be unimportant.
    //
    // More problematic, until UpdateGap terminates, no observation can be
    // selected for sampling. However, because the gap is randomly generated,
    // it is highly unlikely that a bias would emerge from this particular race.
    if (gap_-- == 0) {
      UpdateGap(num_seen_);
      size_t index = uniform_index_(rnd_gen_);
      reservoir_[index] = value;
    }
  }

 private:
  friend class ObservationsCollector;
  friend class EventLogger;

  Sampler(uint32_t metric_id, const std::string& part_name,
          uint32_t encoding_id, size_t samples)
      : metric_id_(metric_id),
        part_name_(part_name),
        encoding_id_(encoding_id),
        size_(samples),
        reservoir_(new std::atomic<T>[size_]),
        written_(new std::atomic<bool>[size_]),
        num_seen_(0),
        rnd_gen_(random_device_()),
        uniform_zero_to_one_(1.0, 0.0),
        uniform_index_(0, samples - 1) {
    for (size_t i = 0; i < size_; i++) {
      written_[i] = false;
    }
    UpdateGap(size_);
  }

  ValuePart GetValuePart(size_t idx);

  void AppendObservations(std::vector<Observation>* observations) {
    for (size_t i = 0; i < size_; i++) {
      if (written_[i]) {
        Observation observation;
        observation.metric_id = metric_id_;
        // TODO(azani): Figure out how to do the undo function.
        observation.parts.push_back(ObservationPart(part_name_, encoding_id_,
                                                    GetValuePart(i), []() {}));
        observations->push_back(observation);
      }
      written_[i] = false;
    }

    // We want to make sure that UpdateGap(num_seen_) in LogObservation is not
    // executed after we call UpdateGap(size_) lower down. (This would result in
    // a potentially too large first gap in the new collection period.)
    for (;;) {
      int64_t gap_val = gap_;
      // If gap_ is negative, that means UpdateGap(num_seen_) in LogObservation
      // might be running. We want to wait until that is over.
      if (gap_val <= -1) {
        continue;
      }
      // If gap_val has not changed, we set it to a negative value (to prevent
      // any new thread from entering the gap_-- == 0 if statement in
      // LogObservation.) This effectively locks gap_ until we run UpdateGap.
      if (gap_.compare_exchange_strong(gap_val, -1)) {
        break;
      }
    }
    num_seen_ = 0;
    // Resume sampling.
    UpdateGap(size_);
  }

  void UpdateGap(size_t num_seen) {
    // In order to speed up reservoir sampling, we compute the gaps between
    // samples. The distribution of the gaps can be well approximated by the
    // geometric distribution. See
    // https://erikerlandson.github.io/blog/2015/11/20/very-fast-reservoir-sampling/
    double p;
    double u;
    p = static_cast<double>(size_) / static_cast<double>(num_seen);
    u = uniform_zero_to_one_(rnd_gen_);
    gap_ = std::floor(std::log(u) / std::log(1 - p));
  }

  const uint32_t metric_id_;
  const std::string part_name_;
  const uint32_t encoding_id_;
  // Reservoir size.
  const size_t size_;
  std::unique_ptr<std::atomic<T>[]> reservoir_;
  std::unique_ptr<std::atomic<bool>[]> written_;
  std::atomic<size_t> num_seen_;
  // gap_ is the number of observations to skip before selecting the next
  // observation to sample.
  // When gap_ is negative, sampling is paused. The thread that sets gap_ to -1
  // is responsible for calling UpdateGap. Threads that decrement gap_ below -1
  // should not call UpdateGap.
  std::atomic<int64_t> gap_;
  // Used only to initialize the random engine: rnd_gen_.
  std::random_device random_device_;
  std::default_random_engine rnd_gen_;
  std::uniform_real_distribution<double> uniform_zero_to_one_;
  std::uniform_int_distribution<size_t> uniform_index_;
  // This condition variable is notified when an observation is written before
  // the reservoir is filled.
  std::condition_variable write_notification_;
};

using IntegerSampler = Sampler<int64_t>;

// EventLogger tracks the rate at which a particular event occurs as well
// as how long an event takes and what status it results in.
class EventLogger {
 public:
  // time is the duration of the event being logged in a unit of your choice.
  // status is a numeric status less than equal to the max_status specified
  // when creating an EventLogger.
  void LogEvent(int64_t time, uint32_t status) {
    status_histogram_[status]++;
    timing_sampler_->LogObservation(time);
  }

 private:
  friend class ObservationsCollector;

  // event_metric_id has three parts:
  // "collection_period": integer length of the collection period in
  // nanoseconds.
  // "status": Distribution mapping the status enum to integer.
  // "total": integer number of events logged during collection period.
  // event_timing_metric_id is implemented by a sampler with samples samples.
  EventLogger(uint32_t event_metric_id, uint32_t max_status,
              uint32_t event_timing_metric_id, uint32_t encoding_id,
              size_t samples)
      : event_metric_id_(event_metric_id),
        max_status_(max_status),
        encoding_id_(encoding_id),
        status_histogram_(new std::atomic<int64_t>[max_status_ + 1]),
        timing_sampler_(new Sampler<int64_t>(event_timing_metric_id, "",
                                             encoding_id, samples)),
        start_time_(std::chrono::steady_clock::now()) {
    for (size_t status = 0; status <= max_status_; status++) {
      status_histogram_[status] = 0;
    }
  }

  // Returns an observation which contains the status distribution, collection
  // period, total number of events collected and average time.
  Observation GetEventObservation(double average_time);

  void AppendObservations(std::vector<Observation>* observations);

  const uint32_t event_metric_id_;
  const uint32_t max_status_;
  const uint32_t encoding_id_;
  std::unique_ptr<std::atomic<int64_t>[]> status_histogram_;
  std::shared_ptr<IntegerSampler> timing_sampler_;
  // Beginning of the current collection period.
  std::chrono::steady_clock::time_point start_time_;
};

// A MetricObservers allows you to group together several observers that
// correspond to metric parts.
class MetricObservers {
 public:
  // Makes a Counter associated with this metric.
  // The part_name specified must be the name of an integer part.
  // The encoding_id specified must be the id of an encoding in the cobalt
  // config.
  std::shared_ptr<Counter> MakeCounter(const std::string& part_name,
                                       uint32_t encoding_id);

 private:
  friend class ObservationsCollector;

  explicit MetricObservers(uint32_t id) : id_(id) {}

  // Gets the Observation.
  Observation GetObservation();

  // MetricObservers id.
  uint32_t id_;
  // Map of counters part_name -> Counter.
  std::map<std::string, std::shared_ptr<Counter>> counters_;
};

// An ObservationsCollector tracks various metrics, collects their values into
// observations and sends them.
// ObservationsCollector is responsible for collecting from the observers
// returned by the Make* methods. If the ObservationCollector that made an
// observer is deleted, the observers it made will still accept data but this
// data will not be collected.
class ObservationsCollector {
 public:
  // send_observations will be used to send the collected observations.
  // default_encoding_id is the encoding id used when no other encoding id
  // is used while making Counters or Samplers.
  explicit ObservationsCollector(SendObservationsFn send_observations,
                                 uint32_t default_encoding_id)
      : send_observations_(send_observations),
        default_encoding_id_(default_encoding_id) {}

  // Makes a Counter object for the specified metric id, part name and
  // encoded using the default encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name);

  // Makes a Counter object for the specified metric id, part name and
  // encoded using the specified encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name,
                                       uint32_t encoding_id);

  // Makes an IntegerSampler for the specified metric id, part name and
  // encoded using the specified encoding id. At most, |samples| samples will be
  // collected per collection period.
  std::shared_ptr<IntegerSampler> MakeIntegerSampler(
      uint32_t metric_id, const std::string& part_name, uint32_t encoding_id,
      size_t samples);

  // Makes an IntegerSampler for the specified metric id, part name and
  // encoded using the default encoding id. At most, |samples| samples will be
  // collected per collection period.
  std::shared_ptr<IntegerSampler> MakeIntegerSampler(
      uint32_t metric_id, const std::string& part_name, size_t samples);

  // MakeEventLogger creates an event logger for the specified metrics.
  // event_metric_id must refer to a metric with the following required parts:
  //   "status": An enum type with a Distribution configured. It will reflect
  //   the number of times LogEvent was called with the specified status codes
  //   in a collection period.
  //   "total": An integer which is the number of times LogEvent was called in a
  //   collection period.
  //   "collection_duration_ns": An integer which is the length of the
  //   collection period in nanoseconds.
  // max_status is the largest value you might pass as a status to LogEvent.
  // event_timing_metric_id refers to a metric with a single integer part which
  // is a sampling of the |time| parameter passed to LogEvent. At most |samples|
  // samples will be sent per collection period.
  std::shared_ptr<EventLogger> MakeEventLogger(uint32_t event_metric_id,
                                               uint32_t max_status,
                                               uint32_t event_timing_metric_id,
                                               size_t samples);

  // Starts a new thread that collects and attempts to send metrics every
  // |collection_interval|.
  // Calling Start more than once without first calling Stop has undefined
  // behavior.
  void Start(std::chrono::nanoseconds collection_interval);

  // Instructs the collection thread started by Start to stop and joins that
  // thread after doing one last collection.
  void Stop();

  // CollectAll attempts to collect observations for all MetricObservers
  // created with this collector and send them using |send_observations|.
  void CollectAll();

 private:
  std::shared_ptr<MetricObservers> GetMetricObservers(uint32_t id);

  void CollectLoop(std::chrono::nanoseconds collection_interval);

  // Map of metric id -> MetricObservers.
  std::map<uint32_t, std::shared_ptr<MetricObservers>> metrics_;
  std::vector<std::function<void(std::vector<Observation>*)>>
      reservoir_samplers_;
  std::vector<std::shared_ptr<EventLogger>> event_loggers_;
  // Thread on which the collection loop is run.
  std::thread collection_loop_;
  // Set to false to stop collection.
  bool collection_loop_continue_;
  // Call this function to send observations.
  SendObservationsFn send_observations_;
  // The encoding id to be used when none is specified.
  uint32_t default_encoding_id_;
};

}  // namespace cobalt

#endif  // LIB_COBALT_CPP_OBSERVATIONS_COLLECTOR_H_
