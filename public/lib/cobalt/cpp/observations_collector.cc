// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/observations_collector.h"

namespace cobalt {

ObservationPart Counter::GetObservationPart() {
  // Atomically swaps the value in counter_ for 0 and puts the former value of
  // counter_ in value.
  ValuePart value = ValuePart::MakeInt(counter_.exchange(0));
  // If the undo function is called, it adds |value| back to the counter_.
  return ObservationPart(part_name_, encoding_id_, value,
                         [this, value]() { counter_ += value.GetIntValue(); });
}

std::shared_ptr<Counter> MetricObservers::MakeCounter(
    const std::string& part_name, uint32_t encoding_id) {
  if (counters_.count(part_name) != 0) {
    return nullptr;
  }
  auto counter = std::shared_ptr<Counter>(new Counter(part_name, encoding_id));
  counters_[part_name] = counter;
  return counter;
}

Observation MetricObservers::GetObservation() {
  Observation observation;
  observation.metric_id = id_;

  for (auto iter = counters_.begin(); iter != counters_.end(); iter++) {
    observation.parts.push_back(iter->second->GetObservationPart());
  }

  return observation;
}

Observation EventLogger::GetEventObservation(double average_time) {
  Observation observation;
  observation.metric_id = event_metric_id_;
  auto now = std::chrono::steady_clock::now();
  auto collection_duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_);
  start_time_ = now;

  std::map<uint32_t, int64_t> status_histogram;
  int64_t total = 0;
  for (size_t status = 0; status <= max_status_; status++) {
    int64_t status_num = status_histogram_[status].exchange(0);
    status_histogram[status] = status_num;
    total += status_num;
  }
  observation.parts.push_back(
      ObservationPart("status", encoding_id_,
                      ValuePart::MakeDistribution(status_histogram), []() {}));
  observation.parts.push_back(ObservationPart(
      "total", encoding_id_, ValuePart::MakeInt(total), []() {}));
  observation.parts.push_back(ObservationPart(
      "collection_duration_ns", encoding_id_,
      ValuePart::MakeInt(collection_duration.count()), []() {}));
  observation.parts.push_back(
      ObservationPart("average_time", encoding_id_,
                      ValuePart::MakeDouble(average_time), []() {}));

  return observation;
}

void EventLogger::AppendObservations(std::vector<Observation>* observations) {
  std::vector<Observation> sampler_observations;
  timing_sampler_->AppendObservations(&sampler_observations);
  int64_t total = 0;
  for (auto iter = sampler_observations.begin();
       sampler_observations.end() != iter; iter++) {
    total += iter->parts[0].value.GetIntValue();
  }
  observations->insert(observations->end(), sampler_observations.begin(),
                       sampler_observations.end());
  observations->push_back(
      GetEventObservation(static_cast<double>(total) /
                          static_cast<double>(sampler_observations.size())));
}

template <>
ValuePart Sampler<int64_t>::GetValuePart(size_t idx) {
  return ValuePart::MakeInt(reservoir_[idx]);
}

std::shared_ptr<Counter> ObservationsCollector::MakeCounter(
    uint32_t metric_id, const std::string& part_name, uint32_t encoding_id) {
  return GetMetricObservers(metric_id)->MakeCounter(part_name, encoding_id);
}

std::shared_ptr<Counter> ObservationsCollector::MakeCounter(
    uint32_t metric_id, const std::string& part_name) {
  return MakeCounter(metric_id, part_name, default_encoding_id_);
}

std::shared_ptr<IntegerSampler> ObservationsCollector::MakeIntegerSampler(
    uint32_t metric_id, const std::string& part_name, uint32_t encoding_id,
    size_t samples) {
  auto reservoir_sampler = std::shared_ptr<Sampler<int64_t>>(
      new Sampler<int64_t>(metric_id, part_name, encoding_id, samples));
  reservoir_samplers_.push_back(
      [reservoir_sampler](std::vector<Observation>* observations) {
        reservoir_sampler->AppendObservations(observations);
      });
  return reservoir_sampler;
}

std::shared_ptr<IntegerSampler> ObservationsCollector::MakeIntegerSampler(
    uint32_t metric_id, const std::string& part_name, size_t samples) {
  return MakeIntegerSampler(metric_id, part_name, default_encoding_id_,
                            samples);
}

std::shared_ptr<EventLogger> ObservationsCollector::MakeEventLogger(
    uint32_t event_metric_id, uint32_t max_status,
    uint32_t event_timing_metric_id, size_t samples) {
  auto event_logger = std::shared_ptr<EventLogger>(
      new EventLogger(event_metric_id, max_status, event_timing_metric_id,
                      default_encoding_id_, samples));
  event_loggers_.push_back(event_logger);
  return event_logger;
}

std::shared_ptr<MetricObservers> ObservationsCollector::GetMetricObservers(
    uint32_t metric_id) {
  if (metrics_.count(metric_id) == 0) {
    metrics_[metric_id] =
        std::shared_ptr<MetricObservers>(new MetricObservers(metric_id));
  }
  return metrics_[metric_id];
}

void ObservationsCollector::Start(
    std::chrono::nanoseconds collection_interval) {
  collection_loop_continue_ = true;
  collection_loop_ = std::thread(&ObservationsCollector::CollectLoop, this,
                                 collection_interval);
}

void ObservationsCollector::Stop() {
  collection_loop_continue_ = false;
  collection_loop_.join();
}

void ObservationsCollector::CollectAll() {
  std::vector<Observation> observations;
  for (auto iter = metrics_.begin(); iter != metrics_.end(); iter++) {
    observations.push_back(iter->second->GetObservation());
  }

  for (auto iter = reservoir_samplers_.begin();
       iter != reservoir_samplers_.end(); iter++) {
    (*iter)(&observations);
  }

  for (auto iter = event_loggers_.begin(); event_loggers_.end() != iter;
       iter++) {
    (*iter)->AppendObservations(&observations);
  }
  auto errors = send_observations_(&observations);

  // Undo failed observations.
  for (auto iter = errors.begin(); iter != errors.end(); iter++) {
    for (auto parts_iter = observations[*iter].parts.begin();
         parts_iter != observations[*iter].parts.end(); parts_iter++) {
      parts_iter->undo();
    }
  }
}

void ObservationsCollector::CollectLoop(
    std::chrono::nanoseconds collection_interval) {
  while (collection_loop_continue_) {
    CollectAll();
    // TODO(azani): Add jitter.
    std::this_thread::sleep_for(collection_interval);
  }
  // Collect one more time after being told to stop.
  CollectAll();
}
}  // namespace cobalt
