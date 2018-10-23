// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/legacy_logger_impl.h"

#include "garnet/bin/cobalt/app/utils.h"

#include <type_traits>

namespace cobalt {

using cobalt::TimerManager;
using cobalt::TimerVal;
using fuchsia::cobalt::Status;

namespace {
// Returns a tuple of the names of the three MetricParts used to report a
// Metric with at most one int/float part, one string part and one index part.
// The 0th item will be the name of the int/float part, the 1st item will be the
// name of the string part for the component name, and the 2nd item will be the
// name of the index part that is for the event type index. If the metric is not
// found or the MetricParts do not fit the expected types, a tuple with empty
// strings will be returned.
std::tuple<std::string, std::string, std::string> ThreePartMetricPartNames(
    const Metric* metric) {
  if (!metric || metric->parts_size() > 3) {
    return std::make_tuple("", "", "");
  }
  std::string number_part, component_name_part, index_part;
  for (auto pair : metric->parts()) {
    switch (pair.second.data_type()) {
      case MetricPart::INT:
      case MetricPart::DOUBLE: {
        if (!number_part.empty()) {
          return std::make_tuple("", "", "");
        }
        number_part = pair.first;
        break;
      }
      case MetricPart::STRING: {
        if (!component_name_part.empty()) {
          return std::make_tuple("", "", "");
        }
        component_name_part = pair.first;
        break;
      }
      case MetricPart::INDEX: {
        if (!index_part.empty()) {
          return std::make_tuple("", "", "");
        }
        index_part = pair.first;
        break;
      }
      default:
        return std::make_tuple("", "", "");
    }
  }
  return std::make_tuple(number_part, component_name_part, index_part);
}
}  // namespace

LegacyLoggerImpl::LegacyLoggerImpl(
    std::unique_ptr<encoder::ProjectContext> project_context,
    encoder::ClientSecret client_secret,
    encoder::ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    encoder::ShippingManager* shipping_manager,
    const encoder::SystemData* system_data, TimerManager* timer_manager)
    : encoder_(std::move(project_context), std::move(client_secret),
               system_data),
      observation_store_(observation_store),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_manager_(shipping_manager),
      timer_manager_(timer_manager) {}

template <class ValueType, class CB>
void LegacyLoggerImpl::LogThreePartMetric(const std::string& value_part_name,
                                          uint32_t metric_id,
                                          uint32_t event_type_index,
                                          fidl::StringPtr component,
                                          ValueType value, CB callback,
                                          bool value_part_required) {
  const Metric* metric = encoder_.GetMetric(metric_id);
  if (!metric) {
    FXL_LOG(ERROR) << "There is no metric with ID = " << metric_id << ".";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  const std::string& metric_name = metric->name();

  auto encodings = encoder_.DefaultEncodingsForMetric(metric_id);

  std::string value_part, component_name_part, index_part;
  std::tie(value_part, component_name_part, index_part) =
      ThreePartMetricPartNames(encoder_.GetMetric(metric_id));
  cobalt::encoder::Encoder::Value new_value;

  // LogElapsedTime, LogFrameRate and LogMemoryUsage can be logged to a metric
  // with just a single part while LogEventCount cannot (the user should use
  // LogEvent instead)
  if (encodings.size() == 1 && value_part_required) {
    if (event_type_index != 0 || !component->empty()) {
      FXL_LOG(ERROR)
          << "Metric " << metric_name << " is a single part metric so only "
          << value_part_name
          << " must be provided "
             "(event_type_index must be 0 and component must be empty).";
      callback(Status::INVALID_ARGUMENTS);
      return;
    }
    if (std::is_same<ValueType, int64_t>::value) {
      new_value.AddIntPart(encodings.begin()->second, "", value);
    } else if (std::is_same<ValueType, float>::value) {
      new_value.AddDoublePart(encodings.begin()->second, "", value);
    }
  } else if (encodings.size() == 2 || encodings.size() == 3) {
    if (!value_part.empty()) {
      if (std::is_same<ValueType, int64_t>::value) {
        new_value.AddIntPart(encodings[value_part], value_part, value);
      } else if (std::is_same<ValueType, float>::value) {
        new_value.AddDoublePart(encodings[value_part], value_part, value);
      }
    } else if (value_part_required) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " must have a numeric part to be a valid "
                     << value_part_name << " metric.";
      callback(Status::INVALID_ARGUMENTS);
      return;
    }

    if (!component_name_part.empty()) {
      new_value.AddStringPart(encodings[component_name_part],
                              component_name_part, component);
    } else if (component_name_part.empty() && !component->empty()) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " is a two part metric with no string part so "
                        "component must be empty";
      callback(Status::INVALID_ARGUMENTS);
      return;
    }

    if (!index_part.empty()) {
      new_value.AddIndexPart(encodings[index_part], index_part,
                             event_type_index);
    } else if (index_part.empty() && event_type_index != 0) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " is a two part metric with no index part so "
                        "event_type_index must be 0";
      callback(Status::INVALID_ARGUMENTS);
      return;
    }
  } else {
    FXL_LOG(ERROR) << "Metric " << metric_name << " is not a valid "
                   << value_part_name << " metric.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  auto result = encoder_.Encode(metric_id, new_value);
  AddEncodedObservation(&result, std::move(callback));
}

// Duplicated from cobalt_encoder_impl.cc
template <class CB>
void LegacyLoggerImpl::AddEncodedObservation(
    cobalt::encoder::Encoder::Result* result, CB callback) {
  switch (result->status) {
    case cobalt::encoder::Encoder::kOK:
      break;
    case cobalt::encoder::Encoder::kInsufficientBuildLevel:
      FXL_LOG(WARNING)
          << "Cobalt metric reporting attempt with insufficient build level";
      callback(Status::OK);
      return;
    case cobalt::encoder::Encoder::kInvalidArguments:
      callback(Status::INVALID_ARGUMENTS);
      return;
    case cobalt::encoder::Encoder::kInvalidConfig:
    case cobalt::encoder::Encoder::kEncodingFailed:
      callback(Status::INTERNAL_ERROR);
      FXL_LOG(WARNING) << "Cobalt internal error: " << result->status;
      return;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*(result->observation), message.get())) {
    FXL_LOG(WARNING)
        << "Cobalt internal error. Unable to encrypt observations.";
    callback(Status::INTERNAL_ERROR);
  }
  // AddEncryptedObservation returns a StatusOr<ObservationStore::StoreStatus>.
  auto add_result = observation_store_->AddEncryptedObservation(
      std::move(message), std::move(result->metadata));

  // Unpack the inner StoreStatus and convert it to a cobalt Status.
  Status status = ToCobaltStatus(add_result);
  shipping_manager_->NotifyObservationsAdded();
  callback(status);
}

uint32_t LegacyLoggerImpl::GetSinglePartMetricEncoding(uint32_t metric_id) {
  const Metric* metric = encoder_.GetMetric(metric_id);
  if (!metric) {
    FXL_LOG(ERROR) << "There is no metric with ID = " << metric_id << ".";
    return 0;
  }
  const std::string& metric_name = metric->name();

  auto encodings = encoder_.DefaultEncodingsForMetric(metric_id);
  if (encodings.size() != 1) {
    FXL_LOG(ERROR) << "Expected Metric " << metric_name
                   << " to only have a single part.";
    return 0;
  }

  return encodings.begin()->second;
}

void LegacyLoggerImpl::LogEvent(
    uint32_t metric_id, uint32_t event_type_index,
    fuchsia::cobalt::LoggerBase::LogEventCallback callback) {
  uint32_t encoding_id = GetSinglePartMetricEncoding(metric_id);
  if (encoding_id == 0) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  auto result = encoder_.EncodeIndex(metric_id, encoding_id, event_type_index);
  AddEncodedObservation(&result, std::move(callback));
}

void LegacyLoggerImpl::LogEventCount(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    int64_t period_duration_micros, int64_t count,
    fuchsia::cobalt::LoggerBase::LogEventCountCallback callback) {
  const Metric* metric = encoder_.GetMetric(metric_id);
  if (!metric) {
    FXL_LOG(ERROR) << "There is no metric with ID = " << metric_id << ".";
    return;
  }
  const std::string& metric_name = metric->name();

  if (period_duration_micros != 0) {
    FXL_LOG(ERROR) << "The parameter |period_duration_micros| in the "
                      "method LogEventCount is unsupported in the current "
                      "version of Cobalt. Pass the value 0 for now. Metric="
                   << metric_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  LogThreePartMetric("event count", metric_id, event_type_index, component,
                     count, std::move(callback), false);
}

void LegacyLoggerImpl::LogElapsedTime(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    int64_t elapsed_micros,
    fuchsia::cobalt::LoggerBase::LogElapsedTimeCallback callback) {
  LogThreePartMetric("elapsed time", metric_id, event_type_index, component,
                     elapsed_micros, std::move(callback), true);
}

void LegacyLoggerImpl::LogFrameRate(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    float fps, fuchsia::cobalt::LoggerBase::LogFrameRateCallback callback) {
  LogThreePartMetric("frame rate", metric_id, event_type_index, component, fps,
                     std::move(callback), true);
}

void LegacyLoggerImpl::LogMemoryUsage(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    int64_t bytes,
    fuchsia::cobalt::LoggerBase::LogMemoryUsageCallback callback) {
  LogThreePartMetric("memory usage", metric_id, event_type_index, component,
                     bytes, std::move(callback), true);
}

void LegacyLoggerImpl::LogString(
    uint32_t metric_id, fidl::StringPtr s,
    fuchsia::cobalt::LoggerBase::LogStringCallback callback) {
  uint32_t encoding_id = GetSinglePartMetricEncoding(metric_id);
  if (encoding_id == 0) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  auto result = encoder_.EncodeString(metric_id, encoding_id, s);
  AddEncodedObservation(&result, std::move(callback));
}

template <class CB>
void LegacyLoggerImpl::AddTimerObservationIfReady(
    std::unique_ptr<TimerVal> timer_val_ptr, CB callback) {
  if (!TimerManager::isReady(timer_val_ptr)) {
    // TimerManager has not received both StartTimer and EndTimer calls. Return
    // OK status and wait for the other call.
    callback(Status::OK);
    return;
  }

  auto result = encoder_.EncodeInt(
      timer_val_ptr->metric_id, timer_val_ptr->encoding_id,
      timer_val_ptr->end_timestamp - timer_val_ptr->start_timestamp);
  AddEncodedObservation(&result, std::move(callback));
}

void LegacyLoggerImpl::StartTimer(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    fidl::StringPtr timer_id, uint64_t timestamp, uint32_t timeout_s,
    fuchsia::cobalt::LoggerBase::StartTimerCallback callback) {
  if (event_type_index != 0 || !component->empty()) {
    FXL_LOG(ERROR) << "event_type_index and component are not currently "
                      "consumed. Pass in 0 and empty string respectively.";
    callback(Status::INVALID_ARGUMENTS);
  }
  std::unique_ptr<TimerVal> timer_val_ptr;
  uint32_t encoding_id = GetSinglePartMetricEncoding(metric_id);
  if (encoding_id == 0) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  auto status = timer_manager_->GetTimerValWithStart(
      metric_id, 0, "", encoding_id, timer_id.get(), timestamp, timeout_s,
      &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void LegacyLoggerImpl::EndTimer(
    fidl::StringPtr timer_id, uint64_t timestamp, uint32_t timeout_s,
    fuchsia::cobalt::LoggerBase::EndTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithEnd(timer_id.get(), timestamp,
                                                   timeout_s, &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void LegacyLoggerImpl::LogIntHistogram(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram,
    fuchsia::cobalt::Logger::LogIntHistogramCallback callback) {
  const Metric* metric = encoder_.GetMetric(metric_id);
  if (!metric) {
    FXL_LOG(ERROR) << "There is no metric with ID = " << metric_id << ".";
    return;
  }
  const std::string& metric_name = metric->name();

  if (event_type_index != 0) {
    FXL_LOG(ERROR) << "The parameter |event_type_index| in the method "
                      "LogIntHistogram is unsupported in the current version "
                      "of Cobalt. Pass in the value 0 for now. Metric="
                   << metric_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  if (!component->empty()) {
    FXL_LOG(ERROR) << "The parameter |component| in the method LogIntHistogram "
                      "is unsupported in the current version of Cobalt. Pass "
                      "in an empty string for now. Metric="
                   << metric_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  uint32_t encoding_id = GetSinglePartMetricEncoding(metric_id);
  if (encoding_id == 0) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::map<uint32_t, uint64_t> histogram_map;
  for (auto it = histogram->begin(); histogram->end() != it; it++) {
    histogram_map[(*it).index] = (*it).count;
  }
  auto result = encoder_.EncodeIntBucketDistribution(metric_id, encoding_id,
                                                     histogram_map);
  AddEncodedObservation(&result, std::move(callback));
}

void LegacyLoggerImpl::LogIntHistogram(
    uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
    fidl::VectorPtr<uint32_t> bucket_indices,
    fidl::VectorPtr<uint64_t> bucket_counts,
    fuchsia::cobalt::LoggerSimple::LogIntHistogramCallback callback) {
  const Metric* metric = encoder_.GetMetric(metric_id);
  if (!metric) {
    FXL_LOG(ERROR) << "There is no metric with ID = " << metric_id << ".";
    return;
  }
  const std::string& metric_name = metric->name();

  if (event_type_index != 0) {
    FXL_LOG(ERROR) << "The parameter |event_type_index| in the method "
                      "LogIntHistogram is unsupported in the current version "
                      "of Cobalt. Pass in the value 0 for now. Metric="
                   << metric_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  if (!component->empty()) {
    FXL_LOG(ERROR) << "The parameter |component| in the method LogIntHistogram "
                      "is unsupported in the current version of Cobalt. Pass "
                      "in an empty string for now. Metric="
                   << metric_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  uint32_t encoding_id = GetSinglePartMetricEncoding(metric_id);
  if (encoding_id == 0) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  if (bucket_indices->size() != bucket_counts->size()) {
    FXL_LOG(ERROR) << "[" << metric_id
                   << "]: bucket_indices.size() != bucket_counts.size().";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::map<uint32_t, uint64_t> histogram_map;
  for (auto i = 0; i < bucket_indices->size(); i++) {
    histogram_map[bucket_indices->at(i)] = bucket_counts->at(i);
  }
  auto result = encoder_.EncodeIntBucketDistribution(metric_id, encoding_id,
                                                     histogram_map);
  AddEncodedObservation(&result, std::move(callback));
}

void LegacyLoggerImpl::LogCustomEvent(
    uint32_t metric_id,
    fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values,
    fuchsia::cobalt::Logger::LogCustomEventCallback callback) {
  auto encodings = encoder_.DefaultEncodingsForMetric(metric_id);
  cobalt::encoder::Encoder::Value value;
  for (const auto& event_val : *event_values) {
    switch (event_val.value.Which()) {
      case fuchsia::cobalt::Value::Tag::kStringValue: {
        value.AddStringPart(encodings[event_val.dimension_name],
                            event_val.dimension_name,
                            event_val.value.string_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIntValue: {
        value.AddIntPart(encodings[event_val.dimension_name],
                         event_val.dimension_name, event_val.value.int_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kDoubleValue: {
        value.AddDoublePart(encodings[event_val.dimension_name],
                            event_val.dimension_name,
                            event_val.value.double_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIndexValue: {
        value.AddIndexPart(encodings[event_val.dimension_name],
                           event_val.dimension_name,
                           event_val.value.index_value());
        break;
      }
      default:
        callback(Status::INVALID_ARGUMENTS);
        FXL_LOG(ERROR)
            << "Cobalt: Unrecognized value type for observation part "
            << event_val.dimension_name;
        return;
    }
  }
  auto result = encoder_.Encode(metric_id, value);
  AddEncodedObservation(&result, std::move(callback));
}

}  // namespace cobalt
